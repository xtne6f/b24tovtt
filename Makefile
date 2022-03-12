CXXFLAGS := -std=c++11 -Wall -Wextra -pedantic-errors -O2 $(CXXFLAGS)
LDFLAGS := -Wl,-s $(LDFLAGS)
ifdef MINGW_PREFIX
  LDFLAGS := -municode -static $(LDFLAGS)
  TARGET ?= b24tovtt.exe
else
  LDFLAGS := $(LDFLAGS)
  TARGET ?= b24tovtt
endif

all: $(TARGET)
$(TARGET): b24tovtt.cpp
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) $(LDFLAGS) $(TARGET_ARCH) -o $@ b24tovtt.cpp
clean:
	$(RM) $(TARGET)
