#
# GNU makefile for sample Dynamically Linked UDFs.
#

# PCRE2 (8-bit); PCRE1 (libpcre) is end-of-life
OS := $(shell uname -o)
ifeq ($(OS),Cygwin)
   XCFLAGS = -I/usr/include
   XLDFLAGS = -lpcre2-8
else
   XCFLAGS = $(shell pkg-config --cflags libpcre2-8 2>/dev/null)
   XLDFLAGS = $(shell pkg-config --libs libpcre2-8 2>/dev/null || echo -lpcre2-8)
endif

export XCFLAGS XLDFLAGS 
export PODS=env.pod path.pod

export GSFS=basename.gsf chdir.gsf dirname.gsf exists.gsf fexists.gsf dexists.gsf getatime.gsf \
            getctime.gsf getmtime.gsf glob.gsf isdir.gsf isfile.gsf \
            wordexp.gsf match.gsf sed.gsf \
            setenv.gsf getenv.gsf printenv.gsf abspath.gsf

%:
	@$(MAKE) -f ../gex.mk $@ \
        CSRC="env.c path.c"\
        EXTRAS="pcrs.o"


