//
// ix.h
//
//   Index Manager Component Interface
//

#ifndef IX_H
#define IX_H

#include "redbase.h"  // Please don't change these lines
#include "rm_rid.h"  // Please don't change these lines
#include "pf.h"
#include <string>
#include <cstdlib>
#include <cstring>

// This is the header for the entire index. 
struct IX_IndexHeader{
    AttrType attr_type; // attribute type and length
    int attr_length;

    int entryOffset_N;  // the offset from the header of the beginning of
    int entryOffset_B;  // the entry list in the Bucket and Nodes 

    int keysOffset_N;   // the offset from the header of the beginning of
                        // the keys list in the nodes
    int maxKeys_N;      // Maximum number of entries in buckets and nodes
    int maxKeys_B;

    PageNum rootPage;   // Page number associated with the root page
};

//
// IX_IndexHandle: IX Index File interface
//
class IX_IndexHandle {
    friend class IX_Manager;
    friend class IX_IndexScan;
    static const int BEGINNING_OF_SLOTS = -2; // class constants
    static const int END_OF_SLOTS = -3;
    static const char UNOCCUPIED = 'u';
    static const char OCCUPIED_NEW = 'n';
    static const char OCCUPIED_DUP = 'r';

public:
    IX_IndexHandle();
    ~IX_IndexHandle();

    // Insert a new index entry
    RC InsertEntry(void *pData, const RID &rid);

    // Delete a new index entry
    RC DeleteEntry(void *pData, const RID &rid);

    // Force index files to disk
    RC ForcePages();
private:
    // Given an attribute length, calculates the max number of entries
    // for the bucket and the nodes
    static int CalcNumKeysNode(int attrLength);
    static int CalcNumKeysBucket(int attrLength);
    // checks if the values given in the header (offsets, sizes, etc) make
    // a valid header
    bool isValidIndexHeader() const;

    // Private variables
    bool isOpenHandle;     // Indicator for whether the indexHandle is being used
    PF_FileHandle pfh;     // The PF_FileHandle associated with this index
    bool header_modified;  // Indicator for whether the header has been modified
    PF_PageHandle rootPH;  // The PF_PageHandle associated with the root node
    struct IX_IndexHeader header; // The header for this index

    // The comparator used to compare keys in this index
    int (*comparator) (void * , void *, int);
    bool (*printer) (void *, int);

    // Creates new node and bucket pages
    RC CreateNewNode(PF_PageHandle &ph, PageNum &page, char *& nData, bool isLeaf);
    RC CreateNewBucket(PageNum &page);

    // Splits the node given itself, and the parent node
    RC SplitNode(struct IX_NodeHeader *pHeader, struct IX_NodeHeader *oldHeader, PageNum oldPage, int index, 
        int &newKeyIndex, PageNum &newPageNum);

    // Inserts a value into a non-full node, or a bucket
    RC InsertIntoNonFullNode(struct IX_NodeHeader *nHeader, PageNum thisNodeNum, void *pData, const RID &rid);
    RC InsertIntoBucket(PageNum page, const RID &rid);

    // Find the appropriate index to insert the value into
    RC FindNodeInsertIndex(struct IX_NodeHeader *nHeader, 
        void* pData, int& index, bool& isDup);

};

//
// IX_IndexScan: condition-based scan of index entries
//
class IX_IndexScan {
    static const char UNOCCUPIED = 'u';    // class constants to check whether
    static const char OCCUPIED_NEW = 'n';  // a slot in a node is valid
    static const char OCCUPIED_DUP = 'r';
public:
    IX_IndexScan();
    ~IX_IndexScan();

    // Open index scan
    RC OpenScan(const IX_IndexHandle &indexHandle,
                CompOp compOp,
                void *value,
                ClientHint  pinHint = NO_HINT);


    // Get the next matching entry return IX_EOF if no more matching
    // entries.
    RC GetNextEntry(RID &rid);

    // Close index scan
    RC CloseScan();
};

//
// IX_Manager: provides IX index file management
//
class IX_Manager {
    static const char UNOCCUPIED = 'u';
public:
    IX_Manager(PF_Manager &pfm);
    ~IX_Manager();

    // Create a new Index
    RC CreateIndex(const char *fileName, int indexNo,
                   AttrType attrType, int attrLength);

    // Destroy and Index
    RC DestroyIndex(const char *fileName, int indexNo);

    // Open an Index
    RC OpenIndex(const char *fileName, int indexNo,
                 IX_IndexHandle &indexHandle);

    // Close an Index
    RC CloseIndex(IX_IndexHandle &indexHandle);
private:
    PF_Manager &pfm; // The PF_Manager associated with this index.

    // Checks that the index parameters given (attrtype and length) make
    // a valid index
    bool IsValidIndex(AttrType attrType, int attrLength);

    // Creates the index file name from the filename and index number, and
    // returns it as a string in indexname
    RC GetIndexFileName(const char *fileName, int indexNo, std::string &indexname);
    // Sets up the IndexHandle internal varables when opening an index
    RC SetUpIH(IX_IndexHandle &ih, PF_FileHandle &fh, struct IX_IndexHeader *header);
    // Modifies th IndexHandle internal variables when closing an index
    RC CleanUpIH(IX_IndexHandle &indexHandle);

};

//
// Print-error function
//
void IX_PrintError(RC rc);

#define IX_BADINDEXSPEC         (START_IX_WARN + 0) // Bad Specification for Index File
#define IX_BADINDEXNAME         (START_IX_WARN + 1) // Bad index name
#define IX_INVALIDINDEXHANDLE   (START_IX_WARN + 2) // FileHandle used is invalid
#define IX_INVALIDINDEXFILE     (START_IX_WARN + 3) // Bad index file
#define IX_NODEFULL             (START_IX_WARN + 4) // A node in the file is full
#define IX_BADFILENAME          (START_IX_WARN + 5) // Bad file name
#define IX_INVALIDBUCKET        (START_IX_WARN + 6) // Bucket trying to access is invalid
#define IX_DUPLICATEENTRY       (START_IX_WARN + 7) // Trying to enter a duplicate entry
#define IX_INVALIDSCAN          (START_IX_WARN + 8) // Invalid IX_Indexscsan
#define IX_INVALIDENTRY         (START_IX_WARN + 9) // Entry not in the index
#define IX_EOF                  (START_IX_WARN + 10)// End of index file
#define IX_LASTWARN             IX_EOF

#define IX_ERROR                (START_IX_ERR - 0) // error
#define IX_LASTERROR            IX_ERROR

#endif
