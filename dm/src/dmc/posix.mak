#_ makefile
# Copyright (C) 1984-1994 by Symantec
# Copyright (c) 2000-2012 by Digital Mars
# Written by Walter Bright
# All Rights Reserved
#       for C/C++ compiler parser and preprocessor (SCC, SCPP, SPP and HTOD)

# Suffixes:
#       n       WIN32 EXE
#       nd      WIN32 DLL

# Paths:

CBX=.
NTDLLSHELL=ntdllshell
TK=tk
INCLUDE=-I$(TK) -I$(NTDLLSHELL)/include
# Where scp command copies to
SCPDIR=..\backup
SCPDIR=walter@mercury:dm

# Options:
#       TARGET          one of SCC, SCPP, SPP, HTOD for C, C++, Preprocessor
#       XFLG            other compiler switches
#       OPT             optimization switches
#       DEBUG           debug switches
#       PREC            precompiled header switches
#       LFLAGS          flags to DOS linker
#       LINKN           linker to use for target

TARGET=SPP
XFLG=
OPT=
DEBUG=-g
#LFLAGS=/map/co
LFLAGS=/map/e/f/packcode/noe;
BASE=

##### Tools

# C++ compiler
CC=g++
# Make program
MAKE=make
# Librarian
LIB=lib
# Delete file(s)
DEL=rm
# Make directory
MD=mkdir
# Remove directory
RD=rmdir
# File copy
CP=cp
# De-tabify
DETAB=detab
# Convert line endings to Unix
TOLF=tolf
# Zip
ZIP=zip32
# Copy to another directory
SCP=$(CP)
SCP=\putty\pscp -i c:\.ssh\colossus.ppk
# PVS-Studio command line executable
PVS="c:\Program Files (x86)\PVS-Studio\x64\PVS-Studio"


CFLAGS=$(INCLUDE) $(XFLG) $(OPT) $(DEBUG) -D$(TARGET)

# Makerules:
.c.o:
	$(CC) -c $(CFLAGS) $*.c

################ DUMMY #########################

noparameter: spp


################ RELEASES #########################

release:
	make clean
	make scppn
	make clean
	make sppn
	make clean
	make scppnd
	make clean
	make sppnd
	make clean
	make htodn
	make clean

################ NT COMMAND LINE RELEASE #########################

sppn:
	make TARGET=SPP  OPT=-O2 DEBUG= LFLAGS= spp

sccn:
#       make TARGET=SCC  OPT=-o "DEBUG=-gt -DSTATIC= -Nc" LFLAGS=/noi/noe/map sccn.exe
	make TARGET=SCC  OPT=-o "DEBUG= -DSTATIC= -Nc" LFLAGS=/noi/noe/map sccn.exe

scppn:
#       make TARGET=SCPP OPT=-o "DEBUG=-gt -g -DSTATIC= -Nc" LFLAGS=/noi/noe/map scppn.exe
#       make TARGET=SCPP OPT=-o "DEBUG=-g -DSTATIC= -Nc" LFLAGS=/noi/noe/map/co scppn.exe
	make TARGET=SCPP OPT=-o "DEBUG= -DSTATIC= -Nc" LFLAGS=/noi/noe/map scppn.exe

htodn:
	make TARGET=HTOD XFLG=-DSCPP OPT=-o "DEBUG= -DSTATIC= -Nc" LFLAGS=/noi/noe/map htodn.exe

################ NT COMMAND LINE DEBUG #########################

debcppn:
	make TARGET=SCPP OPT= "DEBUG=-D -g -DMEM_DEBUG" LFLAGS=/noi/noe/co/m scppn.exe

debppn:
	make TARGET=SPP  OPT= "DEBUG=-D -g -DMEM_DEBUG" LFLAGS= spp

debhtodn:
	make TARGET=HTOD XFLG=-DSCPP  OPT= "DEBUG=-D -g -DMEM_DEBUG" LFLAGS=/noi/noe/co/m htodn.exe

#########################################

OBJ1=blklst.o blockopt.o cpp.o debug.o ee.o el.o enum.o err.o \
	evalu8.o eh.o exp.o exp2.o file.o filename.o func.o adl.o \
	getcmd.o ini.o init.o inline.o loadline.o cppman.o msc.o \
	nspace.o nwc.o os.o out.o ph.o ppexp.o pragma.o pseudo.o \
	struct.o symbol.o template.o tk.o token.o type.o dt.o \
	var.o nteh.o rtlsym.o rtti.o scope.o tdbx.o util.o html.o \
	unialpha.o entity.o aa.o ti_achar.o ti_pvoid.o backconfig.o

OBJ2= cgcod.o cod1.o cod2.o cod3.o cod4.o cod5.o cg87.o cgxmm.o code.o \
	cg.o cgelem.o cgcs.o cgcv.o tytostr.o cgen.o cgreg.o \
	cgobj.o cgsched.o objrecor.o iasm.o ptrntab.o newman.o \
	go.o gloop.o gother.o gflow.o gdag.o glocal.o pdata.o cv8.o \
	errmsgs2.o outbuf.o bcomplex.o htod.o speller.o utf.o divcoeff.o

OBJS= $(OBJ1) $(OBJ2)

#########################################

spp: $(OBJS)
	$(CC) -o spp $(OBJS)

#########################################

test.o : test.c

##################### INCLUDE MACROS #####################

CCH= cc.h cdef.h ty.h outbuf.h $(TK)/mem.h $(TK)/list.h $(TK)/vec.h
TOTALH=$(CCH) token.h parser.h el.h scope.h global.h obj.h type.h oper.h cpp.h \
	$(TK)/filespec.h dt.h msgs2.h bcomplex.h total.h

##################### GENERATED SOURCE #####################

elxxx.c cdxxx.c optab.c debtab.c fltables.c tytab.c : \
	cdef.h cc.h oper.h ty.h optabgen.c
	$(CC) -I$(TK) optabgen.c -o optabgen
	./optabgen

msgs2.h msgs2.c : msgsx.c
	$(CC) msgsx.c -o msgsx
	./msgsx

##################### ZIP ################################

RELEASE= scppn sppn

SRC=    tassert.h html.h html.c \
	ini.c cgcv.h cv4.h tdb.h loadline.c ppexp.c errmsgs2.c tdbx.c \
	allocast.h go.h iasm.h cg.c scdll.h objrecor.c rtlsym.c util.c \
	total.h exh.h cpp.h iasm.c type.c enum.c nspace.c err.c \
	filename.c blklst.c tytostr.c ee.c cppman.c msgs.h symbol.c \
	msgsx.c eh.c template.c rtti.c scope.h scope.c token.h outbuf.h \
	outbuf.c trace.c dt.h file.c parser.h cod5.c type.h newman.c out.c \
	ty.h glocal.c pseudo.c init.c nteh.c \
	code.c el.c el.h ptrntab.c cpp.c func.c debug.c struct.c evalu8.c os.c \
	exp2.c blockopt.c exp.c gdag.c gflow.c gother.c global.h oper.h \
	optabgen.c cgcs.c cgcv.c go.c var.c cod2.c cc.h gloop.c msc.c cod3.c \
	code.h cgsched.c cgen.c ph.c cgreg.c inline.c cgcod.c token.c \
	pragma.c cod1.c cg87.c cgxmm.c cod4.c cgobj.c nwc.c getcmd.c tk.c cdef.h \
	cgelem.c rtlsym.h dt.c strtold.c \
	bcomplex.h bcomplex.c adl.c castab.c unialpha.c entity.c \
	aa.h aa.c tinfo.h ti_achar.c htod.c md5.h speller.h speller.c utf.h utf.c \
	ti_pvoid.c xmm.h obj.h code_stub.h code_x86.h platform_stub.c pdata.c cv8.c \
	backconfig.c ph2.c util2.c divcoeff.c cdeflnx.h

TKSRC= $(TK)\mem.h $(TK)\list.h $(TK)\vec.h $(TK)\filespec.h \
	$(TK)\mem.c $(TK)\list.c $(TK)\vec.c $(TK)\filespec.c

SHELLSRC= $(NTDLLSHELL)\include\dllrun.h $(NTDLLSHELL)\include\network.h \
	$(NTDLLSHELL)\include\nsidde.h $(NTDLLSHELL)\include\netspawn.h

LIBSRC= $(NTDLLSHELL)\lib\spwnlnd.lib $(NTDLLSHELL)\lib\netsploc.o

BUILDSRC= sppn.tra sccn.tra htodn.tra scppn.tra makefile

LICENSE= gpl.txt

VERSION=857

zip:
	$(DEL)del dm$(VERSION).zip
	zip -u dm$(VERSION) $(RELEASE)

zipsrc:
	$(DEL) dm$(VERSION)src.zip
	zip -u dm$(VERSION)src $(SRC) tdb.lib $(LICENSE) backend.txt
	zip -u dm$(VERSION)src $(TKSRC)
	zip -u dm$(VERSION)src $(SHELLSRC)
	zip -u dm$(VERSION)src $(LIBSRC)
	zip -u dm$(VERSION)src $(BUILDSRC)

detab:
	detab $(SRC) $(TKSRC) $(SHELLSRC)

tolf:
	tolf $(SRC) $(TKSRC) $(SHELLSRC)

git: detab tolf $(BUILDSRC)
	$(SCP) $(SRC) $(BUILDSRC) tdb.lib $(LICENSE) $(SCPDIR)/Compiler/dm/src/dmc/
	$(SCP) $(TKSRC) $(SCPDIR)/Compiler/dm/src/dmc/tk
	$(SCP) $(SHELLSRC) $(SCPDIR)/Compiler/dm/src/dmc/ntdllshell/include
	$(SCP) $(LIBSRC) $(SCPDIR)/Compiler/dm/src/dmc/ntdllshell/lib

##################### SPECIAL BUILDS #####################

os.o : os.c
	$(CC) -c $(CFLAGS) os.c

strtold.o : strtold.c
	$(CC) -c -O2 strtold.c

divcoeff.o : divcoeff.c
	$(CC) -c -O2 divcoeff.c

################# Source file dependencies ###############

DEP=$(TOTALH) msgs2.h msgs2.c elxxx.c cdxxx.c optab.c debtab.c fltables.c tytab.c

aa.o : aa.h tinfo.h aa.c
backconfig.o : $(DEP) $(backconfig.dep) backconfig.c
bcomplex.o : bcomplex.h bcomplex.c
blklst.o : $(DEP) $(blklst.dep) blklst.c
blockopt.o : $(DEP) $(blockopt.dep) blockopt.c
cg.o : $(DEP) $(cg.dep) cg.c
cgcs.o : $(DEP) $(cgcs.dep) cgcs.c
cgobj.o : $(DEP) $(cgobj.dep) cgobj.c
cgreg.o : $(DEP) $(cgreg.dep) cgreg.c
cod1.o : $(DEP) $(cod1.dep) cod1.c
cod2.o : $(DEP) $(cod2.dep) cod2.c
cod3.o : $(DEP) $(cod3.dep) cod3.c
cod4.o : $(DEP) $(cod4.dep) cod4.c
cod5.o : $(DEP) $(cod5.dep) cod5.c
code.o : $(DEP) $(code.dep) code.c
cv8.o : $(DEP) $(cv8.dep) cv8.c
debug.o : $(DEP) $(debug.dep) debug.c
dt.o : $(DEP) $(dt.dep) dt.c
ee.o : $(DEP) $(ee.dep) ee.c
eh.o : $(DEP) $(eh.dep) eh.c
el.o : $(DEP) $(el.dep) el.c
enum.o : $(DEP) $(enum.dep) enum.c
errmsgs2.o : $(DEP) $(errmsgs2.dep) errmsgs2.c msgs2.c
evalu8.o : $(DEP) $(evalu8.dep) evalu8.c
filename.o : $(DEP) $(filename.dep) filename.c
glocal.o : $(DEP) $(glocal.dep) glocal.c
gloop.o : $(DEP) $(gloop.dep) gloop.c
htod.o : $(DEP) $(htod.dep) htod.c
html.o : $(DEP) $(html.dep) html.c
init.o : $(DEP) $(init.dep) init.c
inline.o : $(DEP) $(inline.dep) inline.c
loadline.o : $(DEP) $(loadline.dep) loadline.c
cppman.o : $(DEP) $(cppman.dep) cppman.c
msc.o : $(DEP) $(msc.dep) msc.c
nteh.o : $(DEP) $(nteh.dep) nteh.c
objrecor.o : $(DEP) $(objrecor.dep) objrecor.c
out.o : $(DEP) $(out.dep) out.c
pdata.o : $(DEP) $(pdata.dep) pdata.c
pragma.o : $(DEP) $(pragma.dep) pragma.c
pseudo.o : $(DEP) $(pseudo.dep) pseudo.c
ptrntab.o : $(DEP) $(ptrntab.dep) iasm.h ptrntab.c
speller.o : speller.h speller.c
template.o : $(DEP) $(template.dep) template.c
ti_achar.o : tinfo.h ti_achar.c
ti_pvoid.o : tinfo.h ti_pvoid.c
tk.o : $(DEP) $(tk.dep) tk.c
type.o : $(DEP) $(type.dep) type.c
utf.o : utf.h utf.c
util.o : $(DEP) $(util.dep) util.c
cgcv.o : $(DEP) $(cgcv.dep) cgcv.c
cgelem.o : $(DEP) $(cgelem.dep) cgelem.c
gdag.o : $(DEP) $(gdag.dep) gdag.c
getcmd.o : $(DEP) $(getcmd.dep) getcmd.c
gother.o : $(DEP) $(gother.dep) gother.c
exp2.o : $(DEP) $(exp2.dep) castab.c exp2.c
err.o : $(DEP) $(err.dep) err.c
func.o : $(DEP) $(func.dep) func.c
adl.o : $(DEP) $(adl.dep) adl.c
nspace.o : $(DEP) $(nspace.dep) nspace.c
nwc.o : $(DEP) $(nwc.dep) nwc.c
iasm.o : $(DEP) $(iasm.dep) iasm.h iasm.c
go.o : $(DEP) $(go.dep) go.c
struct.o : $(DEP) $(struct.dep) struct.c
gflow.o : $(DEP) $(gflow.dep) gflow.c
cgcod.o : $(DEP) $(cgcod.dep) cgcod.c
cgen.o : $(DEP) $(cgen.dep) cgen.c
ph.o : $(DEP) $(ph.dep) ph.c
token.o : $(DEP) $(token.dep) token.c
rtlsym.o : $(DEP) $(rtlsym.dep) rtlsym.c
symbol.o : $(DEP) $(symbol.dep) symbol.c
tytostr.o : $(DEP) $(tytostr.dep) tytostr.c
cg87.o : $(DEP) $(cg87.dep) cg87.c
cgxmm.o : $(DEP) xmm.h $(cgxmm.dep) cgxmm.c
cpp.o : $(DEP) $(cpp.dep) cpp.c
exp.o : $(DEP) $(exp.dep) exp.c
file.o : $(DEP) $(file.dep) file.c
outbuf.o : $(DEP) $(outbuf.dep) outbuf.c
ppexp.o : $(DEP) $(ppexp.dep) ppexp.c
newman.o : $(DEP) $(newman.dep) newman.c
rtti.o : $(DEP) $(rtti.dep) rtti.c
scope.o : $(DEP) $(scope.dep) scope.c
tdbx.o : $(DEP) $(tdbx.dep) tdbx.c
var.o : $(DEP) $(var.dep) var.c
unialpha.o : unialpha.c

################### Utilities ################

clean:
	$(DEL) $(OBJS)
	$(DEL) msgsx optabgen
	$(DEL) msgs2.h msgs2.c elxxx.c cdxxx.c optab.c debtab.c fltables.c tytab.c

###################################
