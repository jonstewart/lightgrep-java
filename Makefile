
BINDIR=bin
SRCDIR=src

JSRCDIR=$(SRCDIR)/java/src/com/lightboxtechnologies/lightgrep
JTESTDIR=$(SRCDIR)/java/test/com/lightboxtechnologies/lightgrep

JAVA_SOURCES=\
	ContextHandle.java \
	ContextOptions.java \
	Handle.java \
	HitCallback.java \
	KeyOptions.java \
	KeywordException.java \
	LibraryLoader.java \
 	ParserHandle.java \
	ProgramHandle.java \
	ProgramOptions.java \
	SearchHit.java \
	Throws.java
JAVA_SOURCES:=$(addprefix $(JSRCDIR)/,$(JAVA_SOURCES))
JAVA_CLASSES=$(patsubst %,$(BINDIR)/%.class,$(basename $(JAVA_SOURCES)))
JAVA_CLASS_NAMES=$(subst /,.,$(subst $(SRCDIR)/java/src/,,$(basename $(JAVA_SOURCES))))

JAVA_TESTS=LightgrepTest.java
JAVA_TESTS:=$(addprefix $(JTESTDIR)/,$(JAVA_TESTS))
JAVA_TEST_CLASSES=$(patsubst %,$(BINDIR)/%.class,$(basename $(JAVA_TESTS)))



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

test: $(LIB) $(JAVA_TEST_CLASSES)
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

$(BINDIR)/src/jni/jlightgrep.h: $(JAVA_CLASSES)
	$(JAVAH) -o $@ -jni -cp $(BINDIR)/src/java/src $(JAVA_CLASS_NAMES)

$(JAVA_CLASSES): $(JAVA_SOURCES) | $(BINDIR)/src/java/src
	$(JAVAC) -d $(BINDIR)/src/java/src -cp $(BINDIR)/src/java/src -Xlint $^

$(JAVA_TEST_CLASSES): $(JAVA_TESTS) | $(BINDIR)/src/java/test
	$(JAVAC) -d $(BINDIR)/src/java/test -cp $(BINDIR)/src/java/src:$(BINDIR)/src/java/test:/usr/share/java/junit.jar -Xlint $^

.PHONY: all clean example lib jar test
