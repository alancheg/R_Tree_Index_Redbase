#include <unistd.h>
#include <sys/types.h>
#include "pf.h"
#include <stdlib.h>
#include <cstdio>
#include <cstring>

static int compare_string(void *value1, void* value2, int attrLength){
  return strncmp((char *) value1, (char *) value2, attrLength);
}

static int compare_int(void *value1, void* value2, int attrLength){
  if((*(int *)value1 < *(int *)value2))
    return -1;
  else if((*(int *)value1 > *(int *)value2))
    return 1;
  else
    return 0;
}

static int compare_float(void *value1, void* value2, int attrLength){
  if((*(float *)value1 < *(float *)value2))
    return -1;
  else if((*(float *)value1 > *(float *)value2))
    return 1;
  else
    return 0;
}

//TODO: add logic to compare mbr
static int compare_mbr(void *value1, void* value2, int attrLength){
  printf("%s\n", "comparing mbr");
  Mbr mbr1 = *(Mbr *)value1; //Mbr to insert
  Mbr mbr2 = *(Mbr *)value2; //existing Mbr

  //If there is a node whose Mbr contains the Mbr to be inserted
  if(mbr2.x_min < mbr1.x_min && mbr2.y_min < mbr1.y_min && mbr2.x_max > mbr1.x_max && mbr2.y_max > mbr1.y_max)
    return -1;
  else
  {
    //Calculate original Mbr area
    int area = (mbr2.x_max - mbr2.x_min)*(mbr2.y_max - mbr2.y_min);
    Mbr newRect;

    newRect.x_min = mbr1.x_min < mbr2.x_min ? mbr1.x_min : mbr2.x_min;
    newRect.y_min = mbr1.y_min < mbr2.y_min ? mbr1.y_min : mbr2.y_min;
    newRect.x_max = mbr1.x_max > mbr2.x_max ? mbr1.x_max : mbr2.x_max;
    newRect.y_max = mbr1.y_max > mbr2.y_max ? mbr1.y_max : mbr2.y_max;
    //new area
    int newArea = (newRect.x_max - newRect.x_min)*(newRect.y_max - newRect.y_min);

    return (newArea - area);
  }

}

static bool print_string(void *value, int attrLength){
  char * str = (char *)malloc(attrLength + 1);
  memcpy(str, value, attrLength+1);
  str[attrLength] = '\0';
  printf("%s ", str);
  free(str);
  return true;
}

static bool print_int(void *value, int attrLength){
  int num = *(int*)value;
  printf("%d ", num);
  return true;
}

static bool print_float(void *value, int attrLength){
  float num = *(float *)value;
  printf("%f ", num);
  return true;
}

static bool print_mbr(void *value, int attrLength){
  Mbr mbr = *(Mbr *)value;
  printf("(%f, %f, %f, %f) ", mbr.x_min, mbr.y_min, mbr.x_max, mbr.y_max);
  return true;
}