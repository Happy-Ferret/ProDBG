#include "../PDReadWrite.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

typedef struct ReaderData
{
	void* data;
	void* dataStart;
	uint8_t* nextEvent;
	unsigned int size;
	// failure params

	int readFail;
	int type;
	int offset;

} ReaderData;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static inline int8_t getS8(uint8_t* ptr)
{
	return (int8_t)ptr[0];
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static inline uint8_t getU8(uint8_t* ptr)
{
	return ptr[0];
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static inline uint16_t getS16(uint8_t* ptr)
{
	int16_t v = (ptr[0] << 8) | ptr[1];
	return v; 
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static inline uint16_t getU16(uint8_t* ptr)
{
	uint16_t v = (ptr[0] << 8) | ptr[1];
	return v; 
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static inline int32_t getS32(uint8_t* ptr)
{
	int32_t v = (ptr[0] << 24) | (ptr[1] << 16) | (ptr[2] << 8) | ptr[3];
	return v; 
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static inline int32_t getU32(uint8_t* ptr)
{
	uint32_t v = (ptr[0] << 24) | (ptr[1] << 16) | (ptr[2] << 8) | ptr[3];
	return v; 
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static inline int64_t getS64(uint8_t* ptr)
{
	int64_t v = ((uint64_t)ptr[0] << 56) | ((uint64_t)ptr[1] << 48) | ((uint64_t)ptr[2] << 40) | ((uint64_t)ptr[3] << 32) |
				(ptr[4] << 24) | (ptr[5] << 16) | (ptr[6]  << 8) | ptr[7];
	return v; 
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static inline uint64_t getU64(uint8_t* ptr)
{
	uint64_t v = ((uint64_t)ptr[0] << 56) | ((uint64_t)ptr[1] << 48) | ((uint64_t)ptr[2] << 40) | ((uint64_t)ptr[3] << 32) |
				(ptr[4] << 24) | (ptr[5] << 16) | (ptr[6]  << 8) | ptr[7];
	return v; 
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static inline uint64_t getOffsetUpper(ReaderData* readerData)
{
	uint64_t t = ((uintptr_t)readerData->data - (uintptr_t)readerData->dataStart);
	return t << 32L;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static uint32_t readGetEvent(struct PDReader* reader)
{
	ReaderData* rData = (ReaderData*)reader->data;
	uint16_t event;
	uint8_t type; 
	uint8_t* data;

	// if this is not set we expect this to be the first event and just read from data

	if (!rData->nextEvent)
		data = rData->data;
	else
		data = rData->nextEvent;

	type = *data;

	if (type != PDReadType_event)
	{
		printf("Unable to read event as type is wrong (expected %d but got %d) all read operations will now fail.\n",
			   PDReadType_event, type);
		return 0;
	}

	event = getU16(data + 1);
	rData->nextEvent = data + getU32(data + 3);
	rData->data += 7; // points to the next of data in the stream

	return event;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static uint8_t* findIdByRange(const char* id, uint8_t* start, uint8_t* end)
{
	while (start < end)
	{
		uint32_t size;
		uint8_t typeId = getU8(start);

		// data is a special case as it has 32-bit size instead of 64k 

		if (typeId == PDReadType_data)
		{
			size = getU32(start + 1);

			if (!strcmp((char*)start + 5, id))
				return start;
		}
		else 
		{
			size = getU16(start + 1);

			if (!strcmp((char*)start + 3, id))
				return start;
		}

		start += size;
	}

	// not found

	return 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static uint8_t* findId(struct PDReader* reader, const char* id, PDReaderIterator it)
{
	ReaderData* rData = (ReaderData*)reader->data;

	// if iterator is 0 we search the event stream,

	if (it == 0)
	{
		// if no iterater we will just search the whole event

		return findIdByRange(id, rData->data, rData->nextEvent);
	}
	else
	{
		uint32_t dataOffset = it >> 32LL;  
		uint32_t size = it & 0xffffffffLL;
		uint8_t* start = rData->dataStart + dataOffset;
		uint8_t* end = start + size;
		return findIdByRange(id, start, end);
	}

	return 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static uint32_t readFindS8(struct PDReader* reader, int8_t* res, const char* id, PDReaderIterator it)
{
	uint8_t type;
	const uint8_t* offset = findId(reader, id, it);

	if (!offset)
		return PDReadStatus_notFound;
	
	type = *offset;



	(void)res;
	return PDReadType_none | PDReadStatus_fail;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static uint32_t readFindU8(struct PDReader* reader, uint8_t* res, const char* id, PDReaderIterator it)
{
	(void)reader;
	(void)res;
	(void)id;
	(void)it;
	return PDReadType_none | PDReadStatus_fail;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static uint32_t readFindS16(struct PDReader* reader, int16_t* res, const char* id, PDReaderIterator it)
{
	(void)reader;
	(void)res;
	(void)id;
	(void)it;
	return PDReadType_none | PDReadStatus_fail;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static uint32_t readFindU16(struct PDReader* reader, uint16_t* res, const char* id, PDReaderIterator it)
{
	(void)reader;
	(void)res;
	(void)id;
	(void)it;
	return PDReadType_none | PDReadStatus_fail;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static uint32_t readFindS32(struct PDReader* reader, int32_t* res, const char* id, PDReaderIterator it)
{
	(void)reader;
	(void)res;
	(void)id;
	(void)it;
	return PDReadType_none | PDReadStatus_fail;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static uint32_t readFindU32(struct PDReader* reader, uint32_t* res, const char* id, PDReaderIterator it)
{
	(void)reader;
	(void)res;
	(void)id;
	(void)it;
	return PDReadType_none | PDReadStatus_fail;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static uint32_t readFindS64(struct PDReader* reader, int64_t* res, const char* id, PDReaderIterator it)
{
	(void)reader;
	(void)res;
	(void)id;
	(void)it;
	return PDReadType_none | PDReadStatus_fail;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static uint32_t readFindU64(struct PDReader* reader, uint64_t* res, const char* id, PDReaderIterator it)
{
	(void)reader;
	(void)res;
	(void)id;
	(void)it;
	return PDReadType_none | PDReadStatus_fail;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static uint32_t readFindFloat(struct PDReader* reader, float* res, const char* id, PDReaderIterator it)
{
	(void)reader;
	(void)res;
	(void)id;
	(void)it;
	return PDReadType_none | PDReadStatus_fail;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static uint32_t readFindDouble(struct PDReader* reader, double* res, const char* id, PDReaderIterator it)
{
	(void)reader;
	(void)res;
	(void)id;
	(void)it;
	return PDReadType_none | PDReadStatus_fail;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static uint32_t readFindString(struct PDReader* reader, const char** res, const char* id, PDReaderIterator it)
{
	(void)reader;
	(void)res;
	(void)id;
	(void)it;
	return PDReadType_none | PDReadStatus_fail;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static uint32_t readFindData(struct PDReader* reader, void** data, uint64_t* size, const char* id, PDReaderIterator it)
{
	(void)reader;
	(void)data;
	(void)size;
	(void)id;
	(void)it;
	return PDReadType_none | PDReadStatus_fail;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static uint32_t readFindArray(struct PDReader* reader, PDReaderIterator* arrayIt, const char* id, PDReaderIterator it)
{
	(void)reader;
	(void)arrayIt;
	(void)id;
	(void)it;
	return PDReadType_none | PDReadStatus_fail;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static uint32_t readS8(struct PDReader* reader, int8_t* res, const char** id, PDReaderIterator* it)
{
	(void)reader;
	(void)res;
	(void)id;
	(void)it;
	return PDReadType_none | PDReadStatus_fail;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static uint32_t readU8(struct PDReader* reader, uint8_t* res, const char** id, PDReaderIterator* it)
{
	(void)reader;
	(void)res;
	(void)id;
	(void)it;
	return PDReadType_none | PDReadStatus_fail;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static uint32_t readS16(struct PDReader* reader, int16_t* res, const char** id, PDReaderIterator* it)
{
	(void)reader;
	(void)res;
	(void)id;
	(void)it;
	return PDReadType_none | PDReadStatus_fail;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static uint32_t readU16(struct PDReader* reader, uint16_t* res, const char** id, PDReaderIterator* it)
{
	(void)reader;
	(void)res;
	(void)id;
	(void)it;
	return PDReadType_none | PDReadStatus_fail;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static uint32_t readS32(struct PDReader* reader, int32_t* res, const char** id, PDReaderIterator* it)
{
	(void)reader;
	(void)res;
	(void)id;
	(void)it;
	return PDReadType_none | PDReadStatus_fail;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static uint32_t readU32(struct PDReader* reader, uint32_t* res, const char** id, PDReaderIterator* it)
{
	(void)reader;
	(void)res;
	(void)id;
	(void)it;
	return PDReadType_none | PDReadStatus_fail;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static uint32_t readS64(struct PDReader* reader, int64_t* res, const char** id, PDReaderIterator* it)
{
	(void)reader;
	(void)res;
	(void)id;
	(void)it;
	return PDReadType_none | PDReadStatus_fail;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static uint32_t readU64(struct PDReader* reader, uint64_t* res, const char** id, PDReaderIterator* it)
{
	(void)reader;
	(void)res;
	(void)id;
	(void)it;
	return PDReadType_none | PDReadStatus_fail;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static uint32_t readFloat(struct PDReader* reader, float* res, const char** id, PDReaderIterator* it)
{
	(void)reader;
	(void)res;
	(void)id;
	(void)it;
	return PDReadType_none | PDReadStatus_fail;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static uint32_t readDouble(struct PDReader* reader, double* res, const char** id, PDReaderIterator* it)
{
	(void)reader;
	(void)res;
	(void)id;
	(void)it;
	return PDReadType_none | PDReadStatus_fail;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static uint32_t readString(struct PDReader* reader, const char** res, const char** id, PDReaderIterator* it)
{
	(void)reader;
	(void)res;
	(void)id;
	(void)it;
	return PDReadType_none | PDReadStatus_fail;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static uint32_t readData(struct PDReader* reader, void** res, unsigned int* size, const char** id, PDReaderIterator* it)
{
	(void)reader;
	(void)res;
	(void)size;
	(void)id;
	(void)it;
	return PDReadType_none | PDReadStatus_fail;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static uint32_t readArray(struct PDReader* reader, PDReaderIterator* arrayIt, const char* id, PDReaderIterator* it)
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
	reader->readGetEvent = readGetEvent;
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

	reader->data = malloc(sizeof(ReaderData));
	memset(reader->data, 0, sizeof(ReaderData));
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void PDBinaryReader_initStream(PDReader* reader, void* data, unsigned int size)
{
	ReaderData* readerData = (ReaderData*)reader->data;
	readerData->data = data;
	readerData->size = size;
}


