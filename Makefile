
BINDIR=bin
SRCDIR=src

LIB=$(BINDIR)/$(SRCDIR)/jni/libjlightgrep.so
LIB_SOURCES=$(SRCDIR)/jni/jlightgrep.cpp
LIB_OBJECTS=$(patsubst %,$(BINDIR)/%.os,$(basename $(LIB_SOURCES)))
LIB_DEPS=$(LIB_OBJECTS:%.os=%d)

OUTDIRS=$(dir $(LIB_OBJECTS))

CXX=g++
JAVA=java
JAVAC=javac
JAVAH=javah
JAR=jar
MKDIR=mkdir

CPPFLAGS=-MMD -MP
CXXFLAGS=-std=c++0x -O3 -W -Wall -Wextra -pedantic -pipe -fPIC
INCLUDES=-isystem /usr/lib/jvm/java-1.7.0-openjdk-1.7.0.6.x86_64/include -isystem /usr/lib/jvm/java-1.7.0-openjdk-1.7.0.6.x86_64/include/linux -I../lightgrep/include -Ibin/src/jni

LDFLAGS=-shared
LDPATHS=-L../lightgrep/lib -L../lightgrep/src/bin/lib
LDLIBS=-llightgrep -licudata -licuuc

all: lib test

debug: CXXFLAGS+=-g
debug: CXXFLAGS:=$(filter-out -O3, $(CXXFLAGS))
debug: all

lib: $(LIB)

test: $(LIB) $(BINDIR)/src/java/test/com/lightboxtechnologies/lightgrep/LightgrepTest.class
	LD_LIBRARY_PATH=../lightgrep/lib $(JAVA) -cp /usr/share/java/junit.jar:bin/src/java/src:bin/src/java/test -Djava.library.path=bin/src/jni:../lightgrep/lib:../lightgrep/bin/src/lib org.junit.runner.JUnitCore com.lightboxtechnologies.lightgrep.LightgrepTest

$(BINDIR)/src/java/src $(BINDIR)/src/java/test:
	$(MKDIR) -p $@

clean:
	$(RM) -r $(BINDIR)/*	

-include $(LIB_DEPS)

$(LIB): $(LIB_OBJECTS) 
	$(CXX) -o $@ $(LDFLAGS) $^ $(LDPATHS) $(LDLIBS)

$(BINDIR)/src/jni/jlightgrep.os: src/jni/jlightgrep.cpp $(BINDIR)/src/jni/jlightgrep.h
	$(CXX) -o $@ -c $(CPPFLAGS) $(CXXFLAGS) $(INCLUDES) $<

$(BINDIR)/src/jni/jlightgrep.h: $(BINDIR)/src/java/src/com/lightboxtechnologies/lightgrep/Lightgrep.class
	$(JAVAH) -o $@ -jni -cp bin/src/java/src com.lightboxtechnologies.lightgrep.Lightgrep

$(BINDIR)/src/java/src/%.class: src/java/src/%.java | $(BINDIR)/src/java/src
	$(JAVAC) -d $(BINDIR)/src/java/src -Xlint $<

$(BINDIR)/src/java/test/com/lightboxtechnologies/lightgrep/LightgrepTest.class: src/java/test/com/lightboxtechnologies/lightgrep/LightgrepTest.java $(BINDIR)/src/java/src/com/lightboxtechnologies/lightgrep/Lightgrep.class | $(BINDIR)/src/java/test
	$(JAVAC) -d $(BINDIR)/src/java/test -Xlint -cp /usr/share/java/junit.jar:$(BINDIR)/src/java/src $<

.PHONY: all clean example lib jar test
