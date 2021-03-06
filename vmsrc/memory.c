
#include "types.h"
#include "trace.h"
#include "constants.h"
#include "specialsignatures.h"
#include "specialclasses.h"
#include "memory.h"
#include "threads.h"
#include "classes.h"
#include "language.h"
#include "configure.h"
#include "interpreter.h"
#include "exceptions.h"

#ifdef VERIFY
static boolean memoryInitialized = false;
#endif

#define NULL_OFFSET 0xFFFF

// Size of stack frame in 2-byte words
#define NORM_SF_SIZE ((sizeof(StackFrame) + 1) / 2)

byte typeSize[] = { 
  4, // 0 == T_REFERENCE
  SF_SIZE, // 1 == T_STACKFRAME
  0, // 2
  0, // 3
  1, // 4 == T_BOOLEAN
  2, // 5 == T_CHAR
  4, // 6 == T_FLOAT
  8, // 7 == T_DOUBLE
  1, // 8 == T_BYTE
  2, // 9 == T_SHORT
  4, // 10 == T_INT
  8  // 11 == T_LONG
};

// Two-byte pointers are used so that pointer
// arithmetic works as expected.

/**
 * Top of the free block list.
 */
static TWOBYTES freeOffset;

/**
 * Beginning of heap.
 */
static TWOBYTES *startPtr;

extern Object *memcheck_allocate (TWOBYTES size);
extern void initialize_state (Object *objRef, TWOBYTES numWords);
extern void deallocate (TWOBYTES *ptr, TWOBYTES size);
extern TWOBYTES *allocate (TWOBYTES size);

inline static void set_class (Object *obj, byte classIndex)
{
  obj->flags = CLASS_MASK & classIndex;
}

inline static void set_array (Object *obj, byte elemSize, byte length)
{
  obj->flags = ARRAY_MASK | ((TWOBYTES) (elemSize - 1) << ELEM_SIZE_SHIFT) | 
               length;
  #ifdef VERIFY
  assert (is_array(obj), MEMORY0);
  #endif
}

/**
 * Checks if the class needs to be initialized.
 * If so, the static initializer is dispatched.
 * Otherwise, an instance of the class is allocated.
 *
 * @param btAddr Back-track PC address, in case
 *               a static initializer needs to be invoked.
 * @return Object reference or <code>null</code> iff
 *         NullPointerException had to be thrown or
 *         static initializer had to be invoked.
 */
Object *new_object_checked (byte classIndex, byte *btAddr)
{
  #if 0
  trace (-1, classIndex, 0);
  #endif
  if (dispatch_static_initializer (get_class_record(classIndex), btAddr))
  {
    return JNULL;
  }
  return new_object_for_class (classIndex);
}

/**
 * Allocates and initializes the state of
 * an object.
 */
Object *new_object_for_class (byte classIndex)
{
  Object *ref;
  TWOBYTES instanceSize;

  instanceSize = get_class_record(classIndex)->classSize;
  ref = memcheck_allocate (instanceSize);
  if (ref == null)
  {
    return JNULL;
  }

  // Initialize default values
  set_class (ref, classIndex);
  #if 0
  trace (-1, classIndex, 1);
  trace (-1, ((short) ref) - 56000, 2);
  trace (-1, (short) get_class_index(ref), 3);
  #endif
  initialize_state (ref, instanceSize);
  #if 0
  trace (-1, (short) get_class_index(ref), 4);
  #endif

  #if DEBUG_OBJECTS
  printf ("new_object_for_class: returning %d\n", (int) ref);
  #endif

  return ref;
}

/**
 * @param numWords Number of 2-byte words used in allocating the object.
 */
void initialize_state (Object *objRef, TWOBYTES numWords)
{
  register TWOBYTES *ptr;

  numWords -= NORM_OBJ_SIZE;
  ptr = ((TWOBYTES *) objRef) + NORM_OBJ_SIZE;
  while (numWords-- > 0)
  {
    *ptr++ = 0x0000;
  }  
}

TWOBYTES get_array_size (byte length, byte elemSize)
{
  return NORM_OBJ_SIZE + (((TWOBYTES) length * elemSize) + 1) / 2;
}

/**
 * Allocates an array. The size of the array is NORM_OBJ_SIZE
 * plus the size necessary to allocate <code>length</code> elements
 * of the given type.
 */
Object *new_primitive_array (byte primitiveType, STACKWORD length)
{
  Object *ref;
  byte elemSize;
  TWOBYTES allocSize;

  #ifdef DEBUG_MEMORY
  printf ("new_primitive_array: %d, %d, %d\n", (int) primitiveType,
          (int) length, (int) freeOffset);
  #endif
  
  // Hack to disallow allocations longer than 255:
  if (length > 0xFF)
  {
    throw_exception (outOfMemoryError);
    return JNULL;
  }
  elemSize = typeSize[primitiveType];
  allocSize = get_array_size ((byte) length, elemSize);
  ref = memcheck_allocate (allocSize);
  if (ref == null)
    return JNULL;
  set_array (ref, elemSize, (byte) length);
  initialize_state (ref, allocSize);
  return ref;
}

void free_array (Object *objectRef)
{
  #ifdef VERIFY
  assert (is_array(objectRef), MEMORY7);
  #endif VERIFY

  deallocate ((TWOBYTES *) objectRef, get_array_size (
              get_array_length (objectRef),
              get_element_size (objectRef)));  
}

/**
 * @param elemType Type of primitive element of multi-dimensional array.
 * @param totalDimensions Same as number of brackets in array class descriptor.
 * @param reqDimensions Number of requested dimensions for allocation.
 */
Object *new_multi_array (byte elemType, byte totalDimensions, 
                         byte reqDimensions, STACKWORD *numElemPtr)
{
  Object *ref;

  #ifdef VERIFY
  assert (totalDimensions >= 1, MEMORY6);
  assert (reqDimensions <= totalDimensions, MEMORY8);
  #endif

  #if 0
  printf ("new_multi_array (%d, %d, %d)\n", (int) elemType, (int) totalDimensions, (int) reqDimensions);
  #endif

  if (reqDimensions == 0)
    return JNULL;

  #if 0
  printf ("num elements: %d\n", (int) *numElemPtr);
  #endif

  if (totalDimensions == 1)
    return new_primitive_array (elemType, *numElemPtr);

  ref = new_primitive_array (T_REFERENCE, *numElemPtr);
  if (ref == JNULL)
    return JNULL;
  while ((*numElemPtr)--)
  {
    ref_array(ref)[*numElemPtr] = ptr2word (
      new_multi_array (elemType, totalDimensions - 1, reqDimensions - 1,
      numElemPtr + 1));
  }
  return ref;
}

void save_word (byte *ptr, byte aSize, STACKWORD aWord)
{
  while (aSize--)
  {
    ptr[aSize] = (byte) (aWord & 0xFF);
    aWord = aWord >> 8;
  }
}

typedef union 
{
  struct
  {
    byte byte0;
    byte byte1;
    byte byte2;
    byte byte3;
  } st;
  STACKWORD word;
} AuxStackUnion;

void make_word (byte *ptr, byte aSize, STACKWORD *aWordPtr)
{
  // This switch statement is 
  // a workaround for a gcc bug.
  switch (aSize)
  {
    case 1:
      // Assume signed
      *aWordPtr = (JINT) ((JBYTE) ptr[0]);
      return;
    case 2:
      // Assume signed
      *aWordPtr = (JINT) ((JSHORT) (((TWOBYTES) ptr[0] << 8) | ptr[1]));
      return;
    #ifdef VERIFY
    default:
      assert (aSize == 4, MEMORY9);
    #endif VERIFY
  }
  #if EMULATE
  ((AuxStackUnion *) aWordPtr)->st.byte0 = ptr[3];  
  ((AuxStackUnion *) aWordPtr)->st.byte1 = ptr[2];  
  ((AuxStackUnion *) aWordPtr)->st.byte2 = ptr[1];  
  ((AuxStackUnion *) aWordPtr)->st.byte3 = ptr[0];  
  #else
  ((AuxStackUnion *) aWordPtr)->st.byte0 = ptr[0];  
  ((AuxStackUnion *) aWordPtr)->st.byte1 = ptr[1];  
  ((AuxStackUnion *) aWordPtr)->st.byte2 = ptr[2];  
  ((AuxStackUnion *) aWordPtr)->st.byte3 = ptr[3];  
  #endif
}

Object *memcheck_allocate (TWOBYTES size)
{
  Object *ref;
  ref = (Object *) allocate (size);
  if (ref == null)
  {
    #ifdef VERIFY
    assert (outOfMemoryError != null, MEMORY5);
    #endif
    throw_exception (outOfMemoryError);
    return JNULL;
  }
  ref->syncInfo = 0;
  #ifdef SAFE
  ref->flags = 0;
  #endif
  return ref;
}

// Notes on allocation:
// 1. It's first-fit.
// 2. First 2 bytes of free block is size.
// 3. Second 2 bytes of free block is abs. offset of next free block.

/**
 * @param ptr Beginning of heap.
 * @param size Size of heap in 2-byte words.
 */
void init_memory (void *ptr, TWOBYTES size)
{
  #ifdef VERIFY
  memoryInitialized = true;
  #endif

  startPtr = ptr;
  freeOffset = NULL_OFFSET;
  #if DEBUG_MEMORY
  printf ("Setting start of memory to %d\n", (int) startPtr);
  printf ("Going to reserve %d words\n", size);
  #endif
  deallocate (startPtr, size);
}

/**
 * @param size Size of object in 2-byte words.
 */
TWOBYTES *allocate (TWOBYTES size)
{
  register TWOBYTES *ptr;
  TWOBYTES *anchorOffsetRef;

  #if DEBUG_MEMORY
  printf ("Allocating %d words.\n", size);
  #endif
  anchorOffsetRef = &freeOffset;
  while (*anchorOffsetRef != NULL_OFFSET)
  { 
    ptr = startPtr + *anchorOffsetRef;
    if (ptr[0] >= size + 2)
    {
      ptr[0] = ptr[0] - size;
      return ptr + ptr[0];
    }
    if (ptr[0] >= size)
    {
      // This is necessary or we could run out of memory.
      *anchorOffsetRef = ptr[1]; 
      return ptr;     
    }
    anchorOffsetRef = &(ptr[1]);
  }
  #if DEBUG_MEMORY
  printf ("No more memory!");
  #endif
  return null;      
}

void deallocate (TWOBYTES *ptr, TWOBYTES size)
{
  #ifdef DEBUG_MEMORY
  printf ("deallocating %d words at %d\n", (int) size, (int) ptr);
  #endif
  // TBD: consolidate free blocks
  ptr[0] = size;
  ptr[1] = freeOffset;
  freeOffset = ptr - startPtr;
}







