CC = clang
CFLAGS = -Wall -Wextra -O2
FRAMEWORKS = -framework ApplicationServices -framework CoreFoundation
TARGET = axtrace

$(TARGET): axtrace.c
	$(CC) $(CFLAGS) -o $(TARGET) axtrace.c $(FRAMEWORKS)

clean:
	rm -f $(TARGET)

.PHONY: clean
