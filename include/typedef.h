#define __TYPEDEF__H__ 1

typedef struct{
	uint8_t numLEDs;
	int     fd_spi;
	uint8_t *pixels;
}APA102;