#ifndef CBUFFER_H
#define CBUFFER_H
#include <stdint.h>
#include "config.h"

class CStorage;

#define BUFFER_STATE_EMPTY 0
#define BUFFER_STATE_FILLING 1
#define BUFFER_STATE_FILLED 2
#define BUFFER_STATE_LOCKED 3

#define ELEMENT_UINT8 0
#define ELEMENT_UINT16 1
#define ELEMENT_UINT32 2
#define ELEMENT_INT32 3
#define ELEMENT_FLOAT 4
#define ELEMENT_FLOAT_D1 5 /* floating-point data with 1 decimal place */
#define ELEMENT_FLOAT_D2 6 /* floating-point data with 2 decimal places */

typedef struct
{
  uint16_t pid;
  uint8_t type;
  uint8_t count;
} ELEMENT_HEAD;

class CBuffer
{
public:
  CBuffer(uint8_t* mem);
  void add(uint16_t pid, uint8_t type, void* values, int bytes, uint8_t count = 1);
  void purge();
  void serialize(CStorage& store);
  typedef void (*NumericVisitor)(uint16_t pid, double v, void* user);
  void forEachNumeric(NumericVisitor vis, void* user);
  uint32_t timestamp;
  uint16_t offset;
  uint8_t total;
  uint8_t state;
private:
  uint8_t* m_data;
};

class CBufferManager
{
public:
  void init();
  void purge();
  void free(CBuffer* slot);
  CBuffer* getFree();
  CBuffer* getOldest();
  CBuffer* getNewest();
  void printStats();
private:
  CBuffer** slots = 0;
  CBuffer* last = 0;
  uint32_t total = 0;
};

#endif
