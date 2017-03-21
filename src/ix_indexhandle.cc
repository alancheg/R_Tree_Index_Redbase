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
    if(nHeader == rHeader)
    {
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
      // Split the current root node into two nodes, and make the parent the new root node
      if((rc = SplitRootNode((struct IX_NodeHeader *&)newInternalData, (struct IX_NodeHeader *&)nHeader, newInternalPage,
        header.rootPage, BEGINNING_OF_SLOTS, unused, unusedPage, pData, rid)))
        return (rc);
      if((rc = pfh.MarkDirty(header.rootPage)) || (rc = pfh.UnpinPage(header.rootPage)))
        return (rc);
      rootPH = newInternalPH; // reset root PF_PageHandle
      header.rootPage = newInternalPage;
      header_modified = true; // New root page has been set, so the index header has been modified
    }
    else
    {
      //TODO:
    }
  }
  else
  {
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
  nHeader->invalid2 = NO_MORE_PAGES;
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
 * This function deals with splitting a node:
 * pHeader - the header of the parent node
 * oldHeader - the header of the full node to be split
 * oldPage - the PageNum of the old node to be split
 * index - the index into which to insert the new node into in the parent node
 * newKeyIndex - the index of the first key that points to the new node
 * newPageNum - the page number of the new node
 */
RC IX_IndexHandle::SplitRootNode(struct IX_NodeHeader *pHeader, struct IX_NodeHeader *oldHeader, PageNum newRootPage,
  PageNum oldPage, int index, int & newKeyIndex, PageNum &newPageNum, void* pData, const RID &rid ){
  RC rc = 0;
  //printf("********* SPLITING ROOT NODE ********* at index %d \n", index);
  bool isLeaf = false;  // Determines if the new page should be a leaf page
  if(oldHeader->isLeafNode == true){
    isLeaf = true;
  }
  //New child Node
  PageNum newPage;  // Creates the new page, and acquires its headers
  struct IX_NodeHeader *newHeader;
  PF_PageHandle newPH;
  if((rc = CreateNewNode(newPH, newPage, (char *&)newHeader, isLeaf))){
    return (rc);
  }
  ((IX_NodeHeader_L *)newHeader)->parentPage = newRootPage;
  ((IX_NodeHeader_L *)oldHeader)->parentPage = newRootPage;
  newPageNum = newPage; // returns new page number

  // Retrieve the appropriate pointers to all the nodes' contents
  struct Node_Entry *pEntries = (struct Node_Entry *) ((char *)pHeader + header.entryOffset_N);
  struct Node_Entry *oldEntries = (struct Node_Entry *) ((char *)oldHeader + header.entryOffset_N);
  struct Node_Entry *newEntries = (struct Node_Entry *) ((char *)newHeader + header.entryOffset_N);
  char *pKeys = (char *)pHeader + header.keysOffset_N;
  char *newKeys = (char *)newHeader + header.keysOffset_N;
  char *oldKeys = (char *)oldHeader + header.keysOffset_N;

  // Linear Split
  // Search through all the entries in the old entries
  // and along each dimension find entry whose rectangle has highest
  // low side and one with lowest high side. Record their separation.
  int prev_idx1 = BEGINNING_OF_SLOTS;
  int curr_idx1 = oldHeader->firstSlotIndex;

  int selectIdx1, selectIdx2;
  Mbr mbr_max_x_min, mbr_max_y_min, mbr_min_x_max, mbr_min_y_max;
  float max_x_min_v = -1000, max_y_min_v = -1000, min_x_max_v = 1000, min_y_max_v = 1000;
  float min_x_min_v = 1000, min_y_min_v = 1000, max_x_max_v = -1000, max_y_max_v = -1000;
  int max_x_min_idx = -1, max_y_min_idx = -1, min_x_max_idx = -1, min_y_max_idx = -1;

  for(int i=0; i < header.maxKeys_N ; i++){
    //read the value of the node
    char *oldValue = oldKeys + header.attr_length * curr_idx1;
    Mbr tempMbr = *(Mbr*)oldValue;
    //LPS1: Find entry with corresponding values
    if( tempMbr.x_min > max_x_min_v )
    {
      max_x_min_v = tempMbr.x_min;
      max_x_min_idx = curr_idx1;
      mbr_max_x_min = *(Mbr*)oldValue;
    }
    if( tempMbr.y_min > max_y_min_v)
    {
      max_y_min_v = tempMbr.y_min;
      max_y_min_idx = curr_idx1;
      mbr_max_y_min = *(Mbr*)oldValue;
    }
    if( tempMbr.x_max < min_x_max_v )
    {
      min_x_max_v = tempMbr.x_max;
      min_x_max_idx = curr_idx1;
      mbr_min_x_max = *(Mbr*)oldValue;
    }
    if( tempMbr.y_max < min_y_max_v)
    {
      min_y_max_v = tempMbr.y_max;
      min_y_max_idx = curr_idx1;
      mbr_min_y_max = *(Mbr*)oldValue;
    }

    //calculate the entire width on both axises
    if( tempMbr.x_min < min_x_min_v )
      min_x_min_v = tempMbr.x_min;
    if( tempMbr.y_min < min_y_min_v)
      min_y_min_v = tempMbr.y_min;
    if( tempMbr.x_max > max_x_max_v )
      max_x_max_v = tempMbr.x_max;
    if( tempMbr.y_max < max_y_max_v)
      min_y_max_v = tempMbr.y_max;

    //LPS2: Normalize
    float x_norm = abs(min_x_max_v - max_x_min_v)/(max_x_max_v - min_x_min_v);
    float y_norm = abs(min_y_max_v - max_y_min_v)/(max_y_max_v - min_y_min_v);
    //LPS3: Select pair with greatest normalized separation
    if( x_norm > y_norm)
    {
      selectIdx1 = max_x_min_idx;
      selectIdx2 = min_x_max_idx;
    }
    else
    {
      selectIdx1 = max_y_min_idx;
      selectIdx2 = min_y_max_idx;
    }
    prev_idx1 = curr_idx1;
    curr_idx1 = oldEntries[prev_idx1].nextSlot;
  }

  //TODO: Divide between two parts 
  //oldEntries[prev_idx1].nextSlot = NO_MORE_SLOTS;
  //set selected index1 as start point of old entries
  PageNum temp = oldEntries[oldHeader->firstSlotIndex].page;
  oldEntries[oldHeader->firstSlotIndex].page = oldEntries[selectIdx1].page;
  oldEntries[selectIdx1].page = temp;
  //set selected index2 at N/2
  temp = oldEntries[header.maxKeys_N/2].page;
  oldEntries[header.maxKeys_N/2].page = oldEntries[selectIdx2].page;
  oldEntries[selectIdx2].page = temp;

  //Setting correct offsets
  prev_idx1 = BEGINNING_OF_SLOTS;
  curr_idx1 = oldHeader->firstSlotIndex;
  for(int i=0; i < header.maxKeys_N/2 ; i++){
    prev_idx1 = curr_idx1;
    curr_idx1 = oldEntries[prev_idx1].nextSlot;
  }

  // This is the key to use in the parent node to point to the new node we're creating
  char *parentKey = oldKeys + curr_idx1*header.attr_length; 

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

  // Insert data to be inserted in the newly copied Node
  if((rc = InsertIntoLeafNode(newHeader, newPage, pData, rid)))
    return (rc);

  // Insert 1st value in the parent key into parent at index specified in parameters
  int loc = pHeader->freeSlotIndex;
  // TODO: this need to be modified, parentkey need to be calculated
  // Search until we reach a key which is unoccupied
  
  int prev_idx = BEGINNING_OF_SLOTS;
  int curr_idx = oldHeader->firstSlotIndex;
  //traverse all the nodes present
  Mbr convexMbr;
  convexMbr.x_min = convexMbr.y_min = 1000;
  convexMbr.x_min = convexMbr.y_min = -1000;
  
  while(curr_idx != NO_MORE_SLOTS){
    char *value = oldKeys + header.attr_length * curr_idx;
    Mbr tempMbr = *(Mbr *) value;
    
    if(convexMbr.x_min > tempMbr.x_min)
      convexMbr.x_min = tempMbr.x_min;
    if(convexMbr.y_min > tempMbr.y_min)
      convexMbr.y_min = tempMbr.y_min;
    if(convexMbr.x_max < tempMbr.x_max)
      convexMbr.x_max = tempMbr.x_max;
    if(convexMbr.y_max < tempMbr.y_max)
      convexMbr.y_max = tempMbr.y_max;

    prev_idx = curr_idx;
    curr_idx = oldEntries[prev_idx].nextSlot;

  }

  memcpy(pKeys + loc * header.attr_length, (char *)&convexMbr, header.attr_length);
  newKeyIndex = loc;  // return the slot location that points to the old node
  pEntries[loc].page = oldPage;
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

  //*** Insert 2nd value in the parent key into parent at index specified in parameters
  loc = pHeader->freeSlotIndex;
  // Search until we reach a key which is unoccupied
  
  prev_idx = BEGINNING_OF_SLOTS;
  curr_idx = newHeader->firstSlotIndex;

  convexMbr.x_min = convexMbr.y_min = 1000;
  convexMbr.x_min = convexMbr.y_min = -1000;
  
  while(curr_idx != NO_MORE_SLOTS){
    char *value = newKeys + header.attr_length * curr_idx;
    Mbr tempMbr = *(Mbr *) value;
    
    if(convexMbr.x_min > tempMbr.x_min)
      convexMbr.x_min = tempMbr.x_min;
    if(convexMbr.y_min > tempMbr.y_min)
      convexMbr.y_min = tempMbr.y_min;
    if(convexMbr.x_max < tempMbr.x_max)
      convexMbr.x_max = tempMbr.x_max;
    if(convexMbr.y_max < tempMbr.y_max)
      convexMbr.y_max = tempMbr.y_max;

    prev_idx = curr_idx;
    curr_idx = newEntries[prev_idx].nextSlot;

  }

  memcpy(pKeys + loc * header.attr_length, (char *)&convexMbr, header.attr_length);
  pEntries[loc].page = newPage;
  pEntries[loc].isValid = OCCUPIED_NEW;
  pHeader->freeSlotIndex = pEntries[loc].nextSlot;
  pEntries[loc].nextSlot = pEntries[index].nextSlot;
  pHeader->num_keys++;

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

  // Search until we reach a key which is unoccupied
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
 * This finds the index in a node in which to insert a key into, given the node
 * header and the key to insert. It returns the index to insert into, and whether
 * there already exists a key of this value in this particular node.
 */
RC IX_IndexHandle::FindNodeDeleteIndex(struct IX_NodeHeader *nHeader, 
  void *pData, int& index){
  //printf("%s\n", "Finding Index in the Node");
  // Setup 
  struct Node_Entry *entries = (struct Node_Entry *)((char *)nHeader + header.entryOffset_N);
  char *keys = ((char *)nHeader + header.keysOffset_N);
  Mbr left_mbr = *(Mbr*)pData;
  //printf("pData: (%f,%f,%f,%f)\n", left_mbr.x_min, left_mbr.y_min, left_mbr.x_max, left_mbr.y_max);
  // Search until we reach a key which is unoccupied
  int prev_idx = BEGINNING_OF_SLOTS;
  int curr_idx = nHeader->firstSlotIndex;
  //just return the matching Mbr index
  while(curr_idx != NO_MORE_SLOTS){
    char *value = keys + header.attr_length * curr_idx;
    Mbr right_mbr = *(Mbr*) value;
    if(left_mbr.x_min == right_mbr.x_min && left_mbr.y_min == right_mbr.y_min && 
            left_mbr.x_max == right_mbr.x_max && left_mbr.y_max == right_mbr.y_max)
    {
      index = curr_idx;
      break;
    }
    prev_idx = curr_idx;
    curr_idx = entries[prev_idx].nextSlot;

  }
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
  RC rc = 0;
  if(! isValidIndexHeader() || isOpenHandle == false)
    return (IX_INVALIDINDEXHANDLE);

  // get root page
  struct IX_NodeHeader *rHeader;
  if((rc = rootPH.GetData((char *&)rHeader))){
    printf("failing here\n");
    return (rc);
  }

  // If the root page is empty, then no entries can exist
  if(rHeader->isEmpty && (! rHeader->isLeafNode) )
    return (IX_INVALIDENTRY);
  if(rHeader->num_keys== 0 && rHeader->isLeafNode)
    return (IX_INVALIDENTRY);

  // toDelete is an indicator for whether to delete this current node
  // because it has no more contents
  bool toDelete = false; 
  if((rc = DeleteFromNode(rHeader, pData, rid, toDelete))) // Delete the value from this node
    return (rc);

  // If the tree is empty, set the current node to a leaf node.
  if(toDelete){
    rHeader->isLeafNode = true;
  }

  return (rc);
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
bool IX_IndexHandle::isValidIndexHeader() const {
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

/*
 * Returns the Open PF_PageHandle and the page number of the first leaf page in 
 * this index
 */
RC IX_IndexHandle::GetFirstLeafPage(PF_PageHandle &leafPH, PageNum &leafPage){
  RC rc = 0;
  struct IX_NodeHeader *rHeader;
  if((rc = rootPH.GetData((char *&)rHeader))){ // retrieve header info
    return (rc);
  }

  // if root node is a leaf:
  if(rHeader->isLeafNode == true){
    leafPH = rootPH;
    leafPage = header.rootPage;
    return (0);
  }

  // Otherwise, search down by always going down the first page in each
  // internal node
  struct IX_NodeHeader_I *nHeader = (struct IX_NodeHeader_I *)rHeader;
  PageNum nextPageNum = nHeader->firstPage;
  PF_PageHandle nextPH;
  if(nextPageNum == NO_MORE_PAGES)
    return (IX_EOF);
  if((rc = pfh.GetThisPage(nextPageNum, nextPH)) || (rc = nextPH.GetData((char *&)nHeader)))
    return (rc);
  while(nHeader->isLeafNode == false){ // if it's not a leaf node, unpin it and go
    PageNum prevPage = nextPageNum;    // to its first child
    nextPageNum = nHeader->firstPage;
    if((rc = pfh.UnpinPage(prevPage)))
      return (rc);
    if((rc = pfh.GetThisPage(nextPageNum, nextPH)) || (rc = nextPH.GetData((char *&)nHeader)))
      return (rc);
  }
  leafPage = nextPageNum;
  leafPH = nextPH;

  return (rc);
}

//May be issue can come in this one...check if any error comes
RC IX_IndexHandle::FindRecordPage(PF_PageHandle &leafPH, PageNum &leafPage, void *key){
  RC rc = 0;
  struct IX_NodeHeader *rHeader;
  if((rc = rootPH.GetData((char *&) rHeader))){ // retrieve header info
    return (rc);
  }
  // if root node is leaf
  if(rHeader->isLeafNode == true){
    leafPH = rootPH;
    leafPage = header.rootPage;
    return (0);
  }

  struct IX_NodeHeader_I *nHeader = (struct IX_NodeHeader_I *)rHeader;
  int index = BEGINNING_OF_SLOTS;
  PageNum nextPageNum;
  PF_PageHandle nextPH;
  if((rc = FindNodeInsertIndex((struct IX_NodeHeader *)nHeader, key, index)))
    return (rc);
  struct Node_Entry *entries = (struct Node_Entry *)((char *)nHeader + header.entryOffset_N);
  if(index == BEGINNING_OF_SLOTS)
    nextPageNum = nHeader->firstPage;
  else
    nextPageNum = entries[index].page;
  if(nextPageNum == NO_MORE_PAGES)
    return (IX_EOF);
  
  if((rc = pfh.GetThisPage(nextPageNum, nextPH)) || (rc = nextPH.GetData((char *&)nHeader)))
    return (rc);

  while(nHeader->isLeafNode == false){
    if((rc = FindNodeInsertIndex((struct IX_NodeHeader *)nHeader, key, index)))
      return (rc);
    
    entries = (struct Node_Entry *)((char *)nHeader + header.entryOffset_N);
    PageNum prevPage = nextPageNum;
    if(index == BEGINNING_OF_SLOTS)
      nextPageNum = nHeader->firstPage;
    else
      nextPageNum = entries[index].page;
    //char *keys = (char *)nHeader + header.keysOffset_N; 
    if((rc = pfh.UnpinPage(prevPage)))
      return (rc);
    if((rc = pfh.GetThisPage(nextPageNum, nextPH)) || (rc = nextPH.GetData((char *&)nHeader)))
      return (rc);
  }
  leafPage = nextPageNum;
  leafPH = nextPH;

  return (rc);
}

/*
 * This function deletes a entry RID/key from a node given its node Header. It returns
 * a boolean toDelete that indicates whether the current node is empty or not, to signal
 * to the caller to delete this node
 */
RC IX_IndexHandle::DeleteFromNode(struct IX_NodeHeader *nHeader, void *pData, const RID &rid, bool &toDelete){
  RC rc = 0;
  toDelete = false;
  if(nHeader->isLeafNode){ // If it's a leaf node, delete it from there
    if((rc = DeleteFromLeaf((struct IX_NodeHeader_L *)nHeader, pData, rid, toDelete))){
      return (rc);
    }
  }
  // else, find the appropriate child node, and delete it from there
  else{
    int prevIndex, currIndex;
    bool isDup;  // Find the index of the chil dnode
    if((rc = FindNodeInsertIndex(nHeader, pData, currIndex)))
      return (rc);
    struct IX_NodeHeader_I *iHeader = (struct IX_NodeHeader_I *)nHeader;
    struct Node_Entry *entries = (struct Node_Entry *)((char *)nHeader + header.entryOffset_N);
    
    PageNum nextNodePage;
    bool useFirstPage = false;
    if(currIndex == BEGINNING_OF_SLOTS){ // Use the first slot in the internal node
      useFirstPage = true;               // as the child that contains this value
      nextNodePage = iHeader->firstPage;
      prevIndex = currIndex;
    }
    else{ // Otherwise, go down the appropraite page. Also retrieve the index of the
          // page before this index for deletion purposes
      if((rc = FindPrevIndex(nHeader, currIndex, prevIndex)))
        return (rc);
      nextNodePage = entries[currIndex].page;
    }

    // Acquire the contents of this child page
    PF_PageHandle nextNodePH;
    struct IX_NodeHeader *nextHeader;
    if((rc = pfh.GetThisPage(nextNodePage, nextNodePH)) || (rc = nextNodePH.GetData((char *&)nextHeader)))
      return (rc);
    bool toDeleteNext = false; // indicator for deleting the child page
    rc = DeleteFromNode(nextHeader, pData, rid, toDeleteNext); // Delete from this child page

    RC rc2 = 0;
    if((rc2 = pfh.MarkDirty(nextNodePage)) || (rc2 = pfh.UnpinPage(nextNodePage)))
      return (rc2);

    if(rc == IX_INVALIDENTRY) // If the entry was not found, tell the caller
      return (rc);

    // If the entry was successfully deleted, check whether to delete this child node
    if(toDeleteNext){
      if((rc = pfh.DisposePage(nextNodePage))){ // if so, dispose of page
        return (rc);
      }
      if(useFirstPage == false){ // If the deleted page was the first page, put the 
                                  // following page into the firstPage slot
        if(prevIndex == BEGINNING_OF_SLOTS)
          nHeader->firstSlotIndex = entries[currIndex].nextSlot;
        else
          entries[prevIndex].nextSlot = entries[currIndex].nextSlot;
        entries[currIndex].nextSlot = nHeader->freeSlotIndex;
        nHeader->freeSlotIndex = currIndex;
      }
      else{ // Otherwise, just delete this page from the sequence of slot pointers
        int firstslot = nHeader->firstSlotIndex;
        nHeader->firstSlotIndex = entries[firstslot].nextSlot;
        iHeader->firstPage = entries[firstslot].page;
        entries[firstslot].nextSlot = nHeader->freeSlotIndex;
        nHeader->freeSlotIndex = firstslot;
      }
      // update counters of this node's contents
      if(nHeader->num_keys == 0){ // If there are no more keys, and we just deleted
        nHeader->isEmpty = true;  // the first page, return the indicator to delete this
        toDelete = true;          // node
      }
      else
        nHeader->num_keys--;
      
    }

  }

  return (rc);
}

/*
 * This function deletes an entry from a leaf given the header of the leaf. It returns
 * in toDelete whether this leaf node is empty, and whether to delete it
 */
RC IX_IndexHandle::DeleteFromLeaf(struct IX_NodeHeader_L *nHeader, void *pData, const RID &rid, bool &toDelete){
  RC rc = 0;
  int prevIndex, currIndex;
  if((rc = FindNodeDeleteIndex((struct IX_NodeHeader *)nHeader, pData, currIndex)))
    return (rc);

  // Setup 
  struct Node_Entry *entries = (struct Node_Entry *)((char *)nHeader + header.entryOffset_N);
  char *key = (char *)nHeader + header.keysOffset_N;

  if(currIndex== nHeader->firstSlotIndex) // Set up previous index for deletion of key
    prevIndex = currIndex;                // purposes
  else{
    if((rc = FindPrevIndex((struct IX_NodeHeader *)nHeader, currIndex, prevIndex)))
      return (rc);
  }

  // if only entry, delete it from the leaf
  if(entries[currIndex].isValid == OCCUPIED_NEW){
    PageNum ridPage;
    SlotNum ridSlot;
    if((rc = rid.GetPageNum(ridPage)) || (rc = rid.GetSlotNum(ridSlot))){
      return (rc);
    }
    // If this RID and key value don't match, then the entry is not there. Return IX_INVALIDENTRY
    int compare = comparator((void*)(key + header.attr_length*currIndex), pData, header.attr_length);
    //printf("compared value: %d\n", compare);
    if(ridPage != entries[currIndex].page || ridSlot != entries[currIndex].slot || compare != 0 )
      return (IX_INVALIDENTRY);

    // Otherwise, delete from leaf page
    if(currIndex == nHeader->firstSlotIndex){
      nHeader->firstSlotIndex = entries[currIndex].nextSlot;
    }
    else
      entries[prevIndex].nextSlot = entries[currIndex].nextSlot;
      
    entries[currIndex].nextSlot = nHeader->freeSlotIndex;
    nHeader->freeSlotIndex = currIndex;
    entries[currIndex].isValid = UNOCCUPIED;
    nHeader->num_keys--; // update the key counter
  }

  if(nHeader->num_keys == 0){ // If the leaf is now empty, 
    toDelete = true;          // return the indicator to delete
  }
  return (0);
}

/*
 * Given a node header, and a valid index, returns the index of the slot
 * directly preceding the given one in prevIndex.
 */
RC IX_IndexHandle::FindPrevIndex(struct IX_NodeHeader *nHeader, int thisIndex, int &prevIndex){
  struct Node_Entry *entries = (struct Node_Entry *)((char *)nHeader + header.entryOffset_N);
  int prev_idx = BEGINNING_OF_SLOTS;
  int curr_idx = nHeader->firstSlotIndex;
  while(curr_idx != thisIndex){
    prev_idx = curr_idx;
    curr_idx = entries[prev_idx].nextSlot;
  } 
  prevIndex = prev_idx;
  return (0);
}
