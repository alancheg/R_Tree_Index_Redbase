//
// File:        ix_indexhandle.cc
// Description: IX_IndexHandle handles manipulations within the index
// Author:      <Your Name Here>
//

#include <unistd.h>
#include <sys/types.h>
#include "ix.h"
#include "pf.h"
#include "comparators.h"
#include <cstdio>
#include "ix_internal.h"
#include <math.h>

IX_IndexHandle::IX_IndexHandle()
{
  isOpenHandle = false;       // indexhandle is initially closed
  header_modified = false;
}

IX_IndexHandle::~IX_IndexHandle()
{
  // Implement this
}

//Logic to add R-tree to be handled here
RC IX_IndexHandle::InsertEntry(void *pData, const RID &rid)
{
  //printf("%s\n", "Insert entry");
  if(isOpenHandle == false){
  }

  // only insert if this is a valid, open indexHandle
  if(! isValidIndexHeader() || isOpenHandle == false)
    return (IX_INVALIDINDEXHANDLE);

  // Retrieve the root header
  RC rc = 0;
  struct IX_NodeHeader *rHeader;
  if((rc = rootPH.GetData((char *&)rHeader))){
    return (rc);
  }
  //printf("%s\n", "Get Root data!!");
  //I1: Find position for new record

  //Choose Leaf Algorithm
  
  //CL1: set nHeader to be the root node
  struct IX_NodeHeader *nHeader = rHeader;
  
  //CL2: Descend until leaf is reached
  while(!nHeader->isLeafNode)
  {
    //printf("%s\n", "checking for leaf node!!");
    PageNum nextNodePage;
    // Retrieve contents of this node
    struct Node_Entry *nodeEntries = (struct Node_Entry *) ((char *)nHeader + header.entryOffset_N);
    char *keys = (char *)nHeader + header.keysOffset_N;
    int subtreeNodeIndex = BEGINNING_OF_SLOTS;
    
    //CL3: Choose subtree
    if((rc = FindSubTreeNode(nHeader, pData, subtreeNodeIndex))) // get appropriate index of subtree node
      return (rc);

    //CL4: choose next node
    if(subtreeNodeIndex == -1)
      break;
    else if(subtreeNodeIndex == BEGINNING_OF_SLOTS)
      nextNodePage = ((struct IX_NodeHeader_I *)nHeader)->firstPage;
    else{
      nextNodePage = nodeEntries[subtreeNodeIndex].page;
    }
    // Read this next page to read from.
    PF_PageHandle nextNodePH;
    struct IX_NodeHeader *nextNodeHeader;
    if((rc = pfh.GetThisPage(nextNodePage, nextNodePH)) || (rc = nextNodePH.GetData((char *&)nextNodeHeader)))
      return (rc);
    
    nHeader = nextNodeHeader;
  }

  //printf("%s\n", "Leaf node selected");
  //I2: Add Record to leaf node

  // If the node is full, create a new empty root node
  if(nHeader->num_keys == header.maxKeys_N){
    //printf("%s\n", "If max number of nodes inserted");
    //Check if this is the Root node
    PageNum newInternalPage;
    char *newInternalData;
    PF_PageHandle newInternalPH;
    if((rc = CreateNewNode(newInternalPH, newInternalPage, newInternalData, false))){
      return (rc);
    }
    struct IX_NodeHeader_I *newInternalHeader = (struct IX_NodeHeader_I *)newInternalData;
    newInternalHeader->isEmpty = false;
    //This one is dubious \_(-_-)_\
    
    newInternalHeader->firstPage = header.rootPage; // update the root node

    int unused;
    PageNum unusedPage;
    // Split the current root node into two nodes, and make the parent the new
    // root node
    if((rc = SplitNode((struct IX_NodeHeader *&)newInternalData, (struct IX_NodeHeader *&)nHeader, header.rootPage, 
      BEGINNING_OF_SLOTS, unused, unusedPage)))
      return (rc);
    if((rc = pfh.MarkDirty(header.rootPage)) || (rc = pfh.UnpinPage(header.rootPage)))
      return (rc);
    rootPH = newInternalPH; // reset root PF_PageHandle
    header.rootPage = newInternalPage;
    header_modified = true; // New root page has been set, so the index header has been modified

    // TODO: This insertion part can be moved into SplitNode()
    // Retrieve the contents of the new Root node
    struct IX_NodeHeader *useMe;
    if((rc = newInternalPH.GetData((char *&)useMe))){
      return (rc);
    }
    // Insert into the non-full root node
    if((rc = InsertIntoInternalNode(useMe, header.rootPage, pData, rid)))
      return (rc);
  }
  else{
   // If node is not full, insert into it
    //printf("%s\n", "Inserting in leaf");
    if((rc = InsertIntoLeafNode(nHeader, header.rootPage, pData, rid))){
      //printf("rc in %s: %d\n", "Insertion done", rc);
      return (rc);
    }
  }

  // Mark the root node as dirty
  if((rc = pfh.MarkDirty(header.rootPage)))
    return (rc);

  return (rc);
}


/*
 * This function creates a new page and sets it up as a node. It returns the open
 * PF_PageHandle, the page number, and the pointer to its data. 
 * isLeaf is a boolean that signifies whether this page should be a leaf or not
 */
RC IX_IndexHandle::CreateNewNode(PF_PageHandle &ph, PageNum &page, char *&nData, bool isLeaf)
{
  RC rc = 0;
  if((rc = pfh.AllocatePage(ph)) || (rc = ph.GetPageNum(page))){
    return (rc);
  }
  if((rc = ph.GetData(nData)))
    return (rc);
  struct IX_NodeHeader *nHeader = (struct IX_NodeHeader *)nData;

  nHeader->isLeafNode = isLeaf;
  nHeader->isEmpty = true;
  nHeader->num_keys = 0;
  nHeader->invalid1 = NO_MORE_PAGES;
  nHeader->firstSlotIndex = NO_MORE_SLOTS;
  nHeader->freeSlotIndex = 0;

  struct Node_Entry *entries = (struct Node_Entry *)((char*)nHeader + header.entryOffset_N);

  for(int i=0; i < header.maxKeys_N; i++){ // Sets up the slot pointers into a 
    entries[i].isValid = UNOCCUPIED;       // linked list in the freeSlotIndex list
    entries[i].page = NO_MORE_PAGES;
    if(i == (header.maxKeys_N -1))
      entries[i].nextSlot = NO_MORE_SLOTS;
    else
      entries[i].nextSlot = i+1;
  }

  return (rc);
}

/*
 * This function deals with splitting a node:
 * pHeader - the header of the parent node
 * oldHeader - the header of the full node to be split
 * oldPage - the PageNum of the old node to be split
 * index - the index into which to insert the new node into in the parent node
 * newKeyIndex - the index of the first key that points to the new node
 * newPageNum - the page number of the new node
 */
RC IX_IndexHandle::SplitNode(struct IX_NodeHeader *pHeader, struct IX_NodeHeader *oldHeader, 
  PageNum oldPage, int index, int & newKeyIndex, PageNum &newPageNum){
  RC rc = 0;
  //printf("********* SPLIT ********* at index %d \n", index);
  bool isLeaf = false;  // Determines if the new page should be a leaf page
  if(oldHeader->isLeafNode == true){
    isLeaf = true;
  }
  PageNum newPage;  // Creates the new page, and acquires its headers
  struct IX_NodeHeader *newHeader;
  PF_PageHandle newPH;
  if((rc = CreateNewNode(newPH, newPage, (char *&)newHeader, isLeaf))){
    return (rc);
  }
  newPageNum = newPage; // returns new page number

  // Retrieve the appropriate pointers to all the nodes' contents
  struct Node_Entry *pEntries = (struct Node_Entry *) ((char *)pHeader + header.entryOffset_N);
  struct Node_Entry *oldEntries = (struct Node_Entry *) ((char *)oldHeader + header.entryOffset_N);
  struct Node_Entry *newEntries = (struct Node_Entry *) ((char *)newHeader + header.entryOffset_N);
  char *pKeys = (char *)pHeader + header.keysOffset_N;
  char *newKeys = (char *)newHeader + header.keysOffset_N;
  char *oldKeys = (char *)oldHeader + header.keysOffset_N;

  // Keep the first header.masKeys_N/2 values in the old node
  int prev_idx1 = BEGINNING_OF_SLOTS;
  int curr_idx1 = oldHeader->firstSlotIndex;
  for(int i=0; i < header.maxKeys_N/2 ; i++){
    prev_idx1 = curr_idx1;
    curr_idx1 = oldEntries[prev_idx1].nextSlot;
  }
  oldEntries[prev_idx1].nextSlot = NO_MORE_SLOTS;

  // This is the key to use in the parent node to point to the new node we're creating
  char *parentKey = oldKeys + curr_idx1*header.attr_length; 
  
  //char * tempchar = (char *)malloc(header.attr_length);
  //memcpy(tempchar, parentKey, header.attr_length);
  //printf("split key: %s \n", tempchar);
  //free(tempchar);

  // If we're not splitting a leaf node, then update the firstPageNum pointer in
  // the new internal node's header.
  if(!isLeaf){ 
    struct IX_NodeHeader_I *newIHeader = (struct IX_NodeHeader_I *)newHeader;
    newIHeader->firstPage = oldEntries[curr_idx1].page;
    newIHeader->isEmpty = false;
    prev_idx1 = curr_idx1;
    curr_idx1 = oldEntries[prev_idx1].nextSlot;
    oldEntries[prev_idx1].nextSlot = oldHeader->freeSlotIndex;
    oldHeader->freeSlotIndex = prev_idx1;
    oldHeader->num_keys--;
  }

  // Now, move the remaining header.maxKeys_N/2 values into the new node
  int prev_idx2 = BEGINNING_OF_SLOTS;
  int curr_idx2 = newHeader->freeSlotIndex;
  while(curr_idx1 != NO_MORE_SLOTS){
    newEntries[curr_idx2].page = oldEntries[curr_idx1].page;
    newEntries[curr_idx2].slot = oldEntries[curr_idx1].slot;
    newEntries[curr_idx2].isValid = oldEntries[curr_idx1].isValid;
    memcpy(newKeys + curr_idx2*header.attr_length, oldKeys + curr_idx1*header.attr_length, header.attr_length);
    if(prev_idx2 == BEGINNING_OF_SLOTS){
      newHeader->freeSlotIndex = newEntries[curr_idx2].nextSlot;
      newEntries[curr_idx2].nextSlot = newHeader->firstSlotIndex;
      newHeader->firstSlotIndex = curr_idx2;
    } 
    else{
      newHeader->freeSlotIndex = newEntries[curr_idx2].nextSlot;
      newEntries[curr_idx2].nextSlot = newEntries[prev_idx2].nextSlot;
      newEntries[prev_idx2].nextSlot = curr_idx2;
    }
    prev_idx2 = curr_idx2;  
    curr_idx2 = newHeader->freeSlotIndex; // update insert index

    prev_idx1 = curr_idx1;
    curr_idx1 = oldEntries[prev_idx1].nextSlot;
    oldEntries[prev_idx1].nextSlot = oldHeader->freeSlotIndex;
    oldHeader->freeSlotIndex = prev_idx1;
    oldHeader->num_keys--;
    newHeader->num_keys++;
  }

  // insert parent key into parent at index specified in parameters
  int loc = pHeader->freeSlotIndex;
  memcpy(pKeys + loc * header.attr_length, parentKey, header.attr_length);
  newKeyIndex = loc;  // return the slot location that points to the new node
  pEntries[loc].page = newPage;
  pEntries[loc].isValid = OCCUPIED_NEW;
  if(index == BEGINNING_OF_SLOTS){
    pHeader->freeSlotIndex = pEntries[loc].nextSlot;
    pEntries[loc].nextSlot = pHeader->firstSlotIndex;
    pHeader->firstSlotIndex = loc;
  }
  else{
    pHeader->freeSlotIndex = pEntries[loc].nextSlot;
    pEntries[loc].nextSlot = pEntries[index].nextSlot;
    pEntries[index].nextSlot = loc;
  }
  pHeader->num_keys++;

  // if is leaf node, update the page pointers to the previous and next leaf node
  /*if(isLeaf){
    struct IX_NodeHeader_L *newLHeader = (struct IX_NodeHeader_L *) newHeader;
    struct IX_NodeHeader_L *oldLHeader = (struct IX_NodeHeader_L *) oldHeader;
    newLHeader->nextPage = oldLHeader->nextPage;
    newLHeader->prevPage = oldPage;
    oldLHeader->nextPage = newPage;
    if(newLHeader->nextPage != NO_MORE_PAGES){
      PF_PageHandle nextPH;
      struct IX_NodeHeader_L *nextHeader;
      if((rc = pfh.GetThisPage(newLHeader->nextPage, nextPH)) || (nextPH.GetData((char *&)nextHeader)))
        return (rc);
      nextHeader->prevPage = newPage;
      if((rc = pfh.MarkDirty(newLHeader->nextPage)) || (rc = pfh.UnpinPage(newLHeader->nextPage)))
        return (rc);
    }
  }*/

  // Mark the new page as dirty, and unpin it
  if((rc = pfh.MarkDirty(newPage))||(rc = pfh.UnpinPage(newPage))){
    return (rc);
  }
  return (rc);
}

/*
 * This inserts a value and RID into a node given its header and page number. 
 */
RC IX_IndexHandle::InsertIntoNonFullNode(struct IX_NodeHeader *nHeader, PageNum thisNodeNum, void *pData, 
  const RID &rid){
  RC rc = 0;
  printf("%s\n", "check..insert into non full node");
  // Retrieve contents of this node
  struct Node_Entry *entries = (struct Node_Entry *) ((char *)nHeader + header.entryOffset_N);
  char *keys = (char *)nHeader + header.keysOffset_N;

  // If it is a leaf node, then insert into it
  if(nHeader->isLeafNode){
    int prevInsertIndex = BEGINNING_OF_SLOTS;
    bool isDup = false;
    //if((rc = FindNodeInsertIndex(nHeader, pData, prevInsertIndex, isDup))) // get appropriate index
    //  return (rc);
    // If it's not a duplicate, then insert a new key for it, and update
    // the slot and page values. 
    if(!isDup){
      int index = nHeader->freeSlotIndex;
      memcpy(keys + header.attr_length * index, (char *)pData, header.attr_length);
      entries[index].isValid = OCCUPIED_NEW; // mark it as a single entry
      if((rc = rid.GetPageNum(entries[index].page)) || (rc = rid.GetSlotNum(entries[index].slot)))
        return (rc);
      nHeader->isEmpty = false;
      nHeader->num_keys++;
      nHeader->freeSlotIndex = entries[index].nextSlot;
      if(prevInsertIndex == BEGINNING_OF_SLOTS){
        entries[index].nextSlot = nHeader->firstSlotIndex;
        nHeader->firstSlotIndex = index;
      }
      else{
        entries[index].nextSlot = entries[prevInsertIndex].nextSlot;
        entries[prevInsertIndex].nextSlot = index;
      }
    }
    else{
      printf("%s\n", "duplicate entry");
    }
  }
  else{ // Otherwise, this is a internal node
    // TODO: Whole lot of changes to be done here
    // Get its contents, and find the insert location
    struct IX_NodeHeader_I *nIHeader = (struct IX_NodeHeader_I *)nHeader;
    PageNum nextNodePage;
    int prevInsertIndex = BEGINNING_OF_SLOTS;
    //bool isDup;
    //if((rc = FindNodeInsertIndex(nHeader, pData, prevInsertIndex, isDup)))
    //  return (rc);
    if(prevInsertIndex == BEGINNING_OF_SLOTS)
      nextNodePage = nIHeader->firstPage;
    else{
      nextNodePage = entries[prevInsertIndex].page;
    }

    // Read this next page to insert into.
    PF_PageHandle nextNodePH;
    struct IX_NodeHeader *nextNodeHeader;
    int newKeyIndex;
    PageNum newPageNum;
    if((rc = pfh.GetThisPage(nextNodePage, nextNodePH)) || (rc = nextNodePH.GetData((char *&)nextNodeHeader)))
      return (rc);
    // If this next node is full, the split the node
    if(nextNodeHeader->num_keys == header.maxKeys_N){
      if((rc = SplitNode(nHeader, nextNodeHeader, nextNodePage, prevInsertIndex, newKeyIndex, newPageNum)))
        return (rc);
      char *value = keys + newKeyIndex*header.attr_length;

      // check which of the two split nodes to insert into.
      int compared = comparator(pData, (void *)value, header.attr_length);
      if(compared >= 0){
        PageNum nextPage = newPageNum;
        if((rc = pfh.MarkDirty(nextNodePage)) || (rc = pfh.UnpinPage(nextNodePage)))
          return (rc);
        if((rc = pfh.GetThisPage(nextPage, nextNodePH)) || (rc = nextNodePH.GetData((char *&) nextNodeHeader)))
          return (rc);
        nextNodePage = nextPage;
      }
    }
    // Insert into the following node, then mark it dirty and unpin it
    if((rc = InsertIntoNonFullNode(nextNodeHeader, nextNodePage, pData, rid)))
      return (rc);
    if((rc = pfh.MarkDirty(nextNodePage)) || (rc = pfh.UnpinPage(nextNodePage)))
      return (rc);

  }
  return (rc);
}


/*
 * This inserts a value and RID into leaf node given its header and page number. 
 */
RC IX_IndexHandle::InsertIntoLeafNode(struct IX_NodeHeader *nHeader, PageNum thisNodeNum, void *pData, 
  const RID &rid){
  RC rc = 0;

  // Retrieve contents of this node
  struct Node_Entry *entries = (struct Node_Entry *) ((char *)nHeader + header.entryOffset_N);
  char *keys = (char *)nHeader + header.keysOffset_N;
  // If it is a leaf node, then insert into it
  if(nHeader->isLeafNode){
    //printf("%s\n", "It is Leaf Node..Thank God!!");
    int prevInsertIndex = BEGINNING_OF_SLOTS;
    if((rc = FindNodeInsertIndex(nHeader, pData, prevInsertIndex))) // get appropriate index
      return (rc);
    //printf("%s\n", "Index found inserting data");
    int index = nHeader->freeSlotIndex;
    memcpy(keys + header.attr_length * index, (char *)pData, header.attr_length);
    //printf("%s\n", "data copied");
    entries[index].isValid = OCCUPIED_NEW; // mark it as a single entry
    if((rc = rid.GetPageNum(entries[index].page)) || (rc = rid.GetSlotNum(entries[index].slot)))
      return (rc);
    //printf("rc after %s is %d\n", "get page num and slot number", rc);
    nHeader->isEmpty = false;
    nHeader->num_keys++;
    nHeader->freeSlotIndex = entries[index].nextSlot;
    if(prevInsertIndex == BEGINNING_OF_SLOTS){
      //printf("%s\n", "Inserting at the start entry");
      entries[index].nextSlot = nHeader->firstSlotIndex;
      nHeader->firstSlotIndex = index;
    }
    else{
      //printf("%s\n", "Inserting in the middle of entries");
      entries[index].nextSlot = entries[prevInsertIndex].nextSlot;
      entries[prevInsertIndex].nextSlot = index;
    }
  }
  else
  {
    printf("%s\n", "worng node selected...it is not a leaf node..you moron!!");
  }
  return rc;
}

/*
 * This inserts a value and RID into a node given its header and page number. 
 */
RC IX_IndexHandle::InsertIntoInternalNode(struct IX_NodeHeader *nHeader, PageNum thisNodeNum, void *pData, 
  const RID &rid){
  RC rc = 0;

  // Retrieve contents of this node
  struct Node_Entry *entries = (struct Node_Entry *) ((char *)nHeader + header.entryOffset_N);
  char *keys = (char *)nHeader + header.keysOffset_N;

  // this is a internal node
  // TODO: Whole lot of changes to be done here
  // Get its contents, and find the insert location
  struct IX_NodeHeader_I *nIHeader = (struct IX_NodeHeader_I *)nHeader;
  PageNum nextNodePage;
  int prevInsertIndex = BEGINNING_OF_SLOTS;
  if((rc = FindNodeInsertIndex(nHeader, pData, prevInsertIndex)))
    return (rc);
  if(prevInsertIndex == BEGINNING_OF_SLOTS)
    nextNodePage = nIHeader->firstPage;
  else{
    nextNodePage = entries[prevInsertIndex].page;
  }

  // Read this next page to insert into.
  PF_PageHandle nextNodePH;
  struct IX_NodeHeader *nextNodeHeader;
  int newKeyIndex;
  PageNum newPageNum;
  if((rc = pfh.GetThisPage(nextNodePage, nextNodePH)) || (rc = nextNodePH.GetData((char *&)nextNodeHeader)))
    return (rc);
  // If this next node is full, the split the node
  if(nextNodeHeader->num_keys == header.maxKeys_N){
    if((rc = SplitNode(nHeader, nextNodeHeader, nextNodePage, prevInsertIndex, newKeyIndex, newPageNum)))
      return (rc);
    char *value = keys + newKeyIndex*header.attr_length;

    // check which of the two split nodes to insert into.
    int compared = comparator(pData, (void *)value, header.attr_length);
    if(compared >= 0){
      PageNum nextPage = newPageNum;
      if((rc = pfh.MarkDirty(nextNodePage)) || (rc = pfh.UnpinPage(nextNodePage)))
        return (rc);
      if((rc = pfh.GetThisPage(nextPage, nextNodePH)) || (rc = nextNodePH.GetData((char *&) nextNodeHeader)))
        return (rc);
      nextNodePage = nextPage;
    }
  }
  // Insert into the following node, then mark it dirty and unpin it
  if((rc = InsertIntoNonFullNode(nextNodeHeader, nextNodePage, pData, rid)))
    return (rc);
  if((rc = pfh.MarkDirty(nextNodePage)) || (rc = pfh.UnpinPage(nextNodePage)))
    return (rc);

  return (rc);
}

/*
 * This finds the index in a node in which to insert a key into, given the node
 * header and the key to insert. It returns the index to insert into, and whether
 * there already exists a key of this value in this particular node.
 */
RC IX_IndexHandle::FindNodeInsertIndex(struct IX_NodeHeader *nHeader, 
  void *pData, int& index){
  //printf("%s\n", "Finding Index in the Node");
  // Setup 
  struct Node_Entry *entries = (struct Node_Entry *)((char *)nHeader + header.entryOffset_N);
  char *keys = ((char *)nHeader + header.keysOffset_N);

  // Search until we reach a key in the node that is greater than the pData entered
  int prev_idx = BEGINNING_OF_SLOTS;
  int curr_idx = nHeader->firstSlotIndex;
  //just return the first uncoccupied index
  while(curr_idx != NO_MORE_SLOTS){
    if(entries[curr_idx].isValid == UNOCCUPIED)
      break;
    prev_idx = curr_idx;
    curr_idx = entries[prev_idx].nextSlot;

  }
  index = prev_idx;
  //printf("%s %d\n", "Found the index: ", index );
  return (0);
}

/*
 * This finds the index in a internal node whose rectange needs least enlargment, given the node
 * header and the key. It returns the index of the node where we have to look further
 */
RC IX_IndexHandle::FindSubTreeNode(struct IX_NodeHeader *nHeader, 
  void *pData, int& index){
  // Setup 
  struct Node_Entry *entries = (struct Node_Entry *)((char *)nHeader + header.entryOffset_N);
  char *keys = ((char *)nHeader + header.keysOffset_N);

  int prev_idx = BEGINNING_OF_SLOTS;
  int curr_idx = nHeader->firstSlotIndex;
  float min_increased = 1000;
  int min_idx = curr_idx;
  float min_area = -1;
  float curr_area = -1;
  while(curr_idx != NO_MORE_SLOTS && curr_idx <= nHeader->num_keys){
    char *value = keys + header.attr_length * curr_idx;
    //compare if the value is occupied or not.
    //If not just insert in it.
    float area_inc = comparator(pData, (void*) value, header.attr_length);
    Mbr cmbr = *(Mbr *)value;
    curr_area = (cmbr.x_max - cmbr.x_min)*(cmbr.y_max - cmbr.y_min);
    if(area_inc == -1)
    {
      min_increased = -1;
      if(min_idx == -1)
      {
        min_idx = curr_idx;
        min_area = curr_area;
      } else
      {
        if(curr_area < min_area)
        {
          min_idx = curr_idx;
          min_area = curr_area;
        }
      }
    } else if(area_inc < min_increased)
    {
      min_idx = curr_idx;
      min_area = curr_area + area_inc;
      min_increased = area_inc;
    } else if(area_inc == min_increased)
    {
      if(curr_area + area_inc < min_area)
        {
          min_idx = curr_idx;
          min_area = curr_area + area_inc;
        }
    }

    prev_idx = curr_idx;
    curr_idx = entries[prev_idx].nextSlot;

  }
  index = min_idx;
  return (0);
}


RC IX_IndexHandle::DeleteEntry(void *pData, const RID &rid)
{
  // Implement this
}

RC IX_IndexHandle::ForcePages()
{
  // Implement this
}

/*
 * Calculates the number of keys in a node that it can hold based on a given 
 * attribute length.
 */
int IX_IndexHandle::CalcNumKeysNode(int attrLength)
{
  int body_size = PF_PAGE_SIZE - sizeof(struct IX_NodeHeader);
  return floor(1.0*body_size / (sizeof(struct Node_Entry) + attrLength));
}

/*
 * This function check that the header is a valid header based on the sizes of the attributes,
 * the number of keys, and the offsets. It returns true if it is, and false if it's not
 */
bool IX_IndexHandle::isValidIndexHeader() const
{
  if(header.maxKeys_N <= 0){
    printf("error 1");
    return false;
  }
  if(header.entryOffset_N != sizeof(struct IX_NodeHeader)){
    printf("error 2");
    return false;
  }

  int attrLength2 = (header.keysOffset_N - header.entryOffset_N)/(header.maxKeys_N);
  if(attrLength2 != sizeof(struct Node_Entry)){
    printf("error 3");
    return false;
  }
  return true;
}