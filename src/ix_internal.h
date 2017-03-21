//
// ix_internal.h
//
// Index Manager Component Interface - Internal Structures
//

#ifndef IX_INTERNAL_H
#define IX_INTERNAL_H

#include "ix.h"

#define NO_MORE_PAGES -1
#define NO_MORE_SLOTS -1

// The following structs define the headers for each node. 
// IX_NodeHeader is used as a generic cast for all nodes.
// IX_NodeHeader_I is used once we know the node is an internal node
// IX_NodeHeader_L is used once we know the node is a leaf node
struct IX_NodeHeader{
  bool isLeafNode;  // indicator for whether it is a leaf node
  bool isEmpty;     // Whether the node contains pointers or not
  int num_keys;     // number of valid keys the node holds

  int firstSlotIndex; // the pointer to the beginning of the linked list of
                      // valid key/pointer slots
  int freeSlotIndex;  // the pointer to the beginning of free slots
  PageNum invalid1;
  PageNum invalid2;
};

struct IX_NodeHeader_I{
  bool isLeafNode;
  bool isEmpty;     // whether the node has its first page pointer
                    // or not
  int num_keys;

  int firstSlotIndex; 
  int freeSlotIndex;
  PageNum parentPage; // pointer to the parent page
  PageNum firstPage; // first leaf page under this internal node
};

struct IX_NodeHeader_L{
  bool isLeafNode;
  bool isEmpty; 
  int num_keys;

  int firstSlotIndex;
  int freeSlotIndex;
  PageNum parentPage; // pointer to parent page
  PageNum invalid1;
};

// The following are "entries", or the headers that provide
// information about each slot
struct Entry{
  char isValid;
  int nextSlot;
};

struct Node_Entry{
  char isValid;     // Whether the slot is valid, contains a duplicate
                    // value, or a single value
  int nextSlot;     // Pointer to the next slot in the node
  PageNum page;     // Pointer to the page associated with this key
  SlotNum slot;     // Pointer to the slot associated with this entry
                    // (only valid for leaf nodes)
};

#endif