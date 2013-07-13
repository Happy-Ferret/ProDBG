#include "../PDReadWrite.h"
#include <stdlib.h>
#include <string.h>

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

typedef struct ReadData
{
	int foo;
} ReadData;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static uint32_t readIteratorBeginEvent(struct PDReader* reader, struct PDReaderIterator** it)
{
	(void)reader;
	(void)it;
	return PDReadType_none | PDReadStatus_fail;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static uint32_t readIteratorNextEvent(struct PDReader* reader, struct PDReaderIterator* it)
{
	(void)reader;
	(void)it;
	return PDReadType_none | PDReadStatus_fail;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static uint32_t readIteratorBegin(struct PDReader* reader, struct PDReaderIterator** it, const char** keyName, struct PDReaderIterator* parentIt)
{
	(void)reader;
	(void)it;
	(void)keyName;
	(void)parentIt;
	return PDReadType_none | PDReadStatus_fail;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static uint32_t readIteratorNext(struct PDReader* reader, const char** keyName, struct PDReaderIterator* it)
{
	(void)reader;
	(void)keyName;
	(void)it;
	return PDReadType_none | PDReadStatus_fail;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static uint32_t readFindS8(struct PDReader* reader, int8_t* res, const char* id, struct PDReaderIterator* it)
{
	(void)reader;
	(void)res;
	(void)id;
	(void)it;
	return PDReadType_none | PDReadStatus_fail;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static uint32_t readFindU8(struct PDReader* reader, uint8_t* res, const char* id, struct PDReaderIterator* it)
{
	(void)reader;
	(void)res;
	(void)id;
	(void)it;
	return PDReadType_none | PDReadStatus_fail;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static uint32_t readFindS16(struct PDReader* reader, int16_t* res, const char* id, struct PDReaderIterator* it)
{
	(void)reader;
	(void)res;
	(void)id;
	(void)it;
	return PDReadType_none | PDReadStatus_fail;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static uint32_t readFindU16(struct PDReader* reader, uint16_t* res, const char* id, struct PDReaderIterator* it)
{
	(void)reader;
	(void)res;
	(void)id;
	(void)it;
	return PDReadType_none | PDReadStatus_fail;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static uint32_t readFindS32(struct PDReader* reader, int32_t* res, const char* id, struct PDReaderIterator* it)
{
	(void)reader;
	(void)res;
	(void)id;
	(void)it;
	return PDReadType_none | PDReadStatus_fail;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static uint32_t readFindU32(struct PDReader* reader, uint32_t* res, const char* id, struct PDReaderIterator* it)
{
	(void)reader;
	(void)res;
	(void)id;
	(void)it;
	return PDReadType_none | PDReadStatus_fail;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static uint32_t readFindS64(struct PDReader* reader, int64_t* res, const char* id, struct PDReaderIterator* it)
{
	(void)reader;
	(void)res;
	(void)id;
	(void)it;
	return PDReadType_none | PDReadStatus_fail;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static uint32_t readFindU64(struct PDReader* reader, uint64_t* res, const char* id, struct PDReaderIterator* it)
{
	(void)reader;
	(void)res;
	(void)id;
	(void)it;
	return PDReadType_none | PDReadStatus_fail;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static uint32_t readFindFloat(struct PDReader* reader, float* res, const char* id, struct PDReaderIterator* it)
{
	(void)reader;
	(void)res;
	(void)id;
	(void)it;
	return PDReadType_none | PDReadStatus_fail;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static uint32_t readFindDouble(struct PDReader* reader, double* res, const char* id, struct PDReaderIterator* it)
{
	(void)reader;
	(void)res;
	(void)id;
	(void)it;
	return PDReadType_none | PDReadStatus_fail;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static uint32_t readFindString(struct PDReader* reader, const char* res, const char* id, struct PDReaderIterator* it)
{
	(void)reader;
	(void)res;
	(void)id;
	(void)it;
	return PDReadType_none | PDReadStatus_fail;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static uint32_t readFindData(struct PDReader* reader, void* data, uint64_t* size, const char* id, struct PDReaderIterator* it)
{
	(void)reader;
	(void)data;
	(void)size;
	(void)id;
	(void)it;
	return PDReadType_none | PDReadStatus_fail;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static uint32_t readFindArray(struct PDReader* reader, struct PDReaderIterator** arrayIt, const char* id, struct PDReaderIterator* it)
{
	(void)reader;
	(void)arrayIt;
	(void)id;
	(void)it;
	return PDReadType_none | PDReadStatus_fail;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static uint32_t readS8(struct PDReader* reader, int8_t* res, const char** id, struct PDReaderIterator* it)
{
	(void)reader;
	(void)res;
	(void)id;
	(void)it;
	return PDReadType_none | PDReadStatus_fail;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static uint32_t readU8(struct PDReader* reader, uint8_t* res, const char** id, struct PDReaderIterator* it)
{
	(void)reader;
	(void)res;
	(void)id;
	(void)it;
	return PDReadType_none | PDReadStatus_fail;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static uint32_t readS16(struct PDReader* reader, int16_t* res, const char** id, struct PDReaderIterator* it)
{
	(void)reader;
	(void)res;
	(void)id;
	(void)it;
	return PDReadType_none | PDReadStatus_fail;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static uint32_t readU16(struct PDReader* reader, uint16_t* res, const char** id, struct PDReaderIterator* it)
{
	(void)reader;
	(void)res;
	(void)id;
	(void)it;
	return PDReadType_none | PDReadStatus_fail;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static uint32_t readS32(struct PDReader* reader, int32_t* res, const char** id, struct PDReaderIterator* it)
{
	(void)reader;
	(void)res;
	(void)id;
	(void)it;
	return PDReadType_none | PDReadStatus_fail;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static uint32_t readU32(struct PDReader* reader, uint32_t* res, const char** id, struct PDReaderIterator* it)
{
	(void)reader;
	(void)res;
	(void)id;
	(void)it;
	return PDReadType_none | PDReadStatus_fail;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static uint32_t readS64(struct PDReader* reader, int64_t* res, const char** id, struct PDReaderIterator* it)
{
	(void)reader;
	(void)res;
	(void)id;
	(void)it;
	return PDReadType_none | PDReadStatus_fail;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static uint32_t readU64(struct PDReader* reader, uint64_t* res, const char** id, struct PDReaderIterator* it)
{
	(void)reader;
	(void)res;
	(void)id;
	(void)it;
	return PDReadType_none | PDReadStatus_fail;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static uint32_t readFloat(struct PDReader* reader, float* res, const char** id, struct PDReaderIterator* it)
{
	(void)reader;
	(void)res;
	(void)id;
	(void)it;
	return PDReadType_none | PDReadStatus_fail;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static uint32_t readDouble(struct PDReader* reader, double* res, const char* id, struct PDReaderIterator* it)
{
	(void)reader;
	(void)res;
	(void)id;
	(void)it;
	return PDReadType_none | PDReadStatus_fail;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static uint32_t readString(struct PDReader* reader, const char* res, const char** id, struct PDReaderIterator* it)
{
	//
	(void)reader;
	(void)res;
	(void)id;
	(void)it;
	return PDReadType_none | PDReadStatus_fail;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static uint32_t readData(struct PDReader* reader, void* res, uint64_t* size, const char** id, struct PDReaderIterator* it)
{
	(void)reader;
	(void)res;
	(void)size;
	(void)id;
	(void)it;
	return PDReadType_none | PDReadStatus_fail;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static uint32_t readArray(struct PDReader* reader, struct PDReaderIterator** arrayIt, const char* id, struct PDReaderIterator* it)
{
	(void)reader;
	(void)arrayIt;
	(void)id;
	(void)it;
	return PDReadType_none | PDReadStatus_fail;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void PDBinaryReader_init(PDReader* reader)
{
	reader->readIteratorBeginEvent = readIteratorBeginEvent;
	reader->readIteratorNextEvent = readIteratorNextEvent;
	reader->readIteratorBegin = readIteratorBegin;
	reader->readIteratorNext = readIteratorNext;
	reader->readFindS8 = readFindS8;
	reader->readFindU8 = readFindU8;
	reader->readFindS16 = readFindS16;
	reader->readFindU16 = readFindU16;
	reader->readFindS32 = readFindS32;
	reader->readFindU32 = readFindU32;
	reader->readFindS64 = readFindS64;
	reader->readFindU64 = readFindU64;
	reader->readFindFloat = readFindFloat;
	reader->readFindDouble = readFindDouble;
	reader->readFindString = readFindString;
	reader->readFindData = readFindData;
	reader->readFindArray = readFindArray;
	reader->readS8 = readS8;
	reader->readU8 = readU8;
	reader->readS16 = readS16;
	reader->readU16 = readU16;
	reader->readS32 = readS32;
	reader->readU32 = readU32;
	reader->readS64 = readS64;
	reader->readU64 = readU64;
	reader->readFloat = readFloat;
	reader->readDouble = readDouble;
	reader->readString = readString;
	reader->readData = readData;
	reader->readArray = readArray;

	reader->data = malloc(sizeof(ReadData));
	memset(reader->data, 0, sizeof(ReadData));
}

