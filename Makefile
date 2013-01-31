# $Id: Makefile,v 1.2 2006-02-16 11:22:33 ta Exp $
#
# Author:  Thor Andreassen <ta@toggle.be>
# Created: 2012-06-23
#
# Description:

TARGETS  = runbot_tracking

CXX      = g++
CXXFLAGS = -O2 -I/usr/include/qt4 -I/usr/include/qt4/QtCore
LDFLAGS  = -lopencv_core -lopencv_highgui -lopencv_video
LDFLAGS += -lopencv_imgproc -lopencv_calib3d -lopencv_features2d
LDFLAGS += -lQtCore


.PHONY: all clean dist-clean

all: runbot_tracking

%.o: %.cpp
	$(CXX) -c -o $@ $^ $(CXXFLAGS)

runbot_tracking: matfiledump.o runbot_tracking.o
	$(CXX) -o $@ $^ $(LDFLAGS)

clean:
	$(RM) *.o *.elf

dist-clean: clean
	$(RM) $(TARGETS)
