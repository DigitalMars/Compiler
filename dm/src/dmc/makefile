#_ makefile
# Copyright (C) 1984-1994 by Symantec
# Copyright (c) 2000-2012 by Digital Mars
# Written by Walter Bright
# All Rights Reserved
#       for C/C++ compiler parser and preprocessor (SCPP, SPP and HTOD)

# Suffixes:
#       n       WIN32 EXE
#       nd      WIN32 DLL

# Paths:

DMCDIR=\dm

CBX=.
NTDLLSHELL=ntdllshell
TK=tk
INCLUDE=-I$(TK);$(NTDLLSHELL)\include;$(DMCDIR)\include
LIBNT=$(DMCDIR)\lib
SNN=$(LIBNT)\snn
# Where scp command copies to
SCPDIR=..\backup
SCPDIR=walter@mercury:dm

# Options:
#       TARGET          one of SCPP, SPP, HTOD for C, C++, Preprocessor
#       XFLG            other compiler switches
#       OPT             optimization switches
#       DEBUG           debug switches
#       NTDLL           dll switches
#       PREC            precompiled header switches
#       LFLAGS          flags to DOS linker
#       LINKN           linker to use for target

TARGET=

###### Flags for C++ compiler
XFLG=
OPT=
DEBUG=-gl
NTDLL=-WD
#PREC=-H -HP32 -HItotal.h -HO
PREC=
CFLAGS=$(INCLUDE) $(XFLG) $(OPT) $(DEBUG) -6 -D$(TARGET) -r -cpp

###### Flags for D compiler

# Additive flags for building Windows DLL
DDLLFLAGS=
# Additive flags for optimization
DOPT=-O -release -inline
# Additive flags for debug build
DDEBUG=
DFLAGS=-betterC -noboundscheck -mv=dmd.backend=. -mv=tk=tk -version=$(TARGET) $(DDLLFLAGS) $(DOPT) $(DDEBUG)

###### Flags for linking

BASE=
#LFLAGS=/map/co
LFLAGS=/map/e/f/packcode/noe;

##### Tools

# D compiler
DMD=dmd
# C++ compiler
CC=dmc
# Make program
MAKE=make
# Librarian
LIB=lib
# Delete file(s)
DEL=del
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

LINKN=$(DMCDIR)\bin\link /de


# Makerules:
.c.obj:
	$(CC) -c $(CFLAGS) $(PREC) $*

.asm.obj:
	$(CC) -c $(CFLAGS) $*

################ DUMMY #########################

noparameter:
	echo no parameter


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

scppn:
#	make TARGET=SCPP OPT=-o "DEBUG=-gt -g -DSTATIC= -Nc" LFLAGS=/noi/noe/map scppn.exe
	make TARGET=SCPP OPT=-o "DEBUG= -DSTATIC= -Nc" LFLAGS=/noi/noe/map/co scppn.exe
#	make TARGET=SCPP OPT=-o "DEBUG= -DSTATIC= -Nc" LFLAGS=/noi/noe/map scppn.exe

sppn:
	make TARGET=SPP  OPT=-o+space DEBUG= LFLAGS=/noi/noe/map sppn.exe

htodn:
	make TARGET=HTOD XFLG=-DSCPP OPT=-o "DEBUG= -DSTATIC= -Nc" LFLAGS=/noi/noe/map htodn.exe

htod:
	make TARGET=HTOD XFLG=-DSCPP OPT=-o "DEBUG= -DSTATIC= -Nc" LFLAGS=/noi/noe/map htod.exe

################ NT COMMAND LINE DEBUG #########################

debcppn:
	make TARGET=SCPP OPT= "DEBUG=-D -g -DMEM_DEBUG" \
	DDEBUG=-debug LFLAGS=/noi/noe/co/m scppn.exe

debppn:
	make TARGET=SPP  OPT= "DEBUG=-D -g -DMEM_DEBUG" \
	DDEBUG=-debug LFLAGS=/noi/noe/co/m sppn.exe

debhtodn:
	make TARGET=HTOD XFLG=-DSCPP  OPT= "DEBUG=-D -g -DMEM_DEBUG" \
	DDEBUG=-debug LFLAGS=/noi/noe/co/m htodn.exe

################ NT DLL RELEASE #########################

scppnd:
	make TARGET=SCPP OPT=-o+space "XFLG=$(NTDLL)" DEBUG= DDEBUG= LFLAGS=/noi/noe/map BASE=18087936 DDLLFLAGS=-version=_WINDLL scppnd.dll
#	make TARGET=SCPP OPT=-o+space "XFLG=$(NTDLL)" DEBUG=-g DDEBUG=-debug LFLAGS=/noi/noe/map/co BASE=18087936 DDLLFLAGS=-version=_WINDLL scppnd.dll

sppnd:
	make TARGET=SPP  OPT=-o+space "XFLG=$(NTDLL)" DEBUG= DDEBUG=-debug LFLAGS=/noi/noe/map BASE=21430272 DDLLFLAGS=-version=_WINDLL sppnd.dll

################ NT DLL DEBUG #########################

debcppnd:
	make TARGET=SCPP OPT= "DEBUG=-g -D -DMEM_DEBUG -DTERMCODE" "XFLG=$(NTDLL)" \
	DDEBUG=-debug LFLAGS=/noi/noe/map/co DDLLFLAGS=-version=_WINDLL scppnd.dll

debppnd:
	make TARGET=SPP  OPT=-o+space "XFLG=$(NTDLL)" \
	DDEBUG=-debug LFLAGS=/noi/noe/map/co DDLLFLAGS=-version=_WINDLL sppnd.dll

#########################################

OBJ1a=dblklst.obj blockopt.obj dcpp.obj debugprint.obj ee.obj elem.obj denum.obj err.obj rawstring.obj
OBJ1b=evalu8.obj fp.obj poptelem.obj eh.obj exp.obj dexp2.obj dfile.obj filename.obj func.obj adl.obj
OBJ1c=dgetcmd.obj ini.obj dinit.obj loadline.obj dmsc.obj precomp.obj dmcdll.obj elpicpie.obj
OBJ1d=nspace.obj dnwc.obj os.obj out.obj dph.obj ppexp.obj dpragma.obj pseudo.obj
OBJ1e=dstruct.obj symbol.obj symtab.obj dtemplate.obj tk.obj xtoken.obj dtype.obj dt.obj phstring.obj
OBJ1f=var.obj nteh.obj drtlsym.obj rtti.obj dscope.obj tdbx.obj dutil.obj html.obj
OBJ1g=unialpha.obj entity.obj backconfig.obj dinline.obj oper.obj parser.obj dtoken.obj

OBJ2a= cgcod.obj cod1.obj cod2.obj cod3.obj cod4.obj cod5.obj cg87.obj cgxmm.obj dcode.obj global.obj
OBJ2b= cg.obj cgelem.obj cgcs.obj dcgcv.obj tytostr.obj cgen.obj cgreg.obj compress.obj codebuilder.obj
OBJ2c= cgobj.obj mscoffobj.obj cgsched.obj objrecor.obj diasm.obj ptrntab.obj newman.obj dwarfdbginf.obj stubobj.obj obj.obj
OBJ2d= go.obj gloop.obj gother.obj gflow.obj gdag.obj glocal.obj gsroa.obj pdata.obj cv8.obj barray.obj goh.obj
OBJ2e= errmsgs2.obj outbuf.obj bcomplex.obj htod.obj dspeller.obj utf.obj divcoeff.obj dvec.obj dlist.obj filespec.obj
OBJ2f= cgcse.obj code_x86.obj dvarstats.obj

ALLOBJS= $(OBJ1a) $(OBJ1b) $(OBJ1c) $(OBJ1d) $(OBJ1e) $(OBJ1f) $(OBJ1g) \
         $(OBJ2a) $(OBJ2b) $(OBJ2c) $(OBJ2d) $(OBJ2e) $(OBJ2f)

#########################################

$(TARGET)n.exe : $(ALLOBJS) $(TARGET)n.lnk $(TARGET)n.def
	$(LINKN) @$*.lnk

$(TARGET)nd.dll : $(ALLOBJS) $(TARGET)nd.lnk $(TARGET)nd.def
	$(LINKN) @$*.lnk


#########################################

$(TARGET)n.def : makefile
	echo NAME $(TARGET)              >  $*.def
	echo SUBSYSTEM CONSOLE	         >> $*.def
	echo EXETYPE NT                  >> $*.def
	echo CODE SHARED EXECUTE         >> $*.def
	echo STUB '$(LIBNT)\NTSTUB.EXE'  >> $*.def
	echo INCLUDE $(TARGET)n.tra	     >> $*.def

$(TARGET)nd.def : makefile
	echo LIBRARY "$*.dll"                           > $*.def
	echo DESCRIPTION '$(TARGET) as a DLL'           >> $*.def
	echo EXETYPE NT                                 >> $*.def
	echo SUBSYSTEM WINDOWS                          >> $*.def
	echo CODE SHARED EXECUTE                        >> $*.def
	echo DATA WRITE                                 >> $*.def
	echo EXPORTS                                    >> $*.def
	echo   _$(TARGET)Version=_DllVersion@0  @1      >> $*.def
	echo   _$(TARGET)Entry=_DllEntry@12     @2      >> $*.def
	echo   _NetSpawnVersion=_NetSpawnVersion@0 @3   >> $*.def
#	echo   _NetSpawnVersion@0              @4      >> $*.def

########################################

$(TARGET)n.lnk : makefile
	echo $(OBJ1a)+                          >  $*.lnk
	echo $(OBJ1b)+                          >> $*.lnk
	echo $(OBJ1c)+                          >> $*.lnk
	echo $(OBJ1d)+                          >> $*.lnk
	echo $(OBJ1e)+                          >> $*.lnk
	echo $(OBJ1f)+                          >> $*.lnk
	echo $(OBJ1g)+                          >> $*.lnk
	echo $(OBJ2a)+                          >> $*.lnk
	echo $(OBJ2b)+                          >> $*.lnk
	echo $(OBJ2c)+                          >> $*.lnk
	echo $(OBJ2d)+                          >> $*.lnk
	echo $(OBJ2e)+                          >> $*.lnk
	echo $(OBJ2f)$(LFLAGS)                  >> $*.lnk
	echo $*                                 >> $*.lnk
	echo $*                                 >> $*.lnk
	echo tdb+                               >> $*.lnk
	echo $(SNN)+                            >> $*.lnk
	echo $(LIBNT)\kernel32+                 >> $*.lnk
	echo $(LIBNT)\user32                    >> $*.lnk
	echo $*.def;                            >> $*.lnk

$(TARGET)nd.lnk : makefile
	echo $(NTDLLSHELL)\lib\netsploc+        >  $*.lnk
	echo $(OBJ1a)+                          >> $*.lnk
	echo $(OBJ1b)+                          >> $*.lnk
	echo $(OBJ1c)+                          >> $*.lnk
	echo $(OBJ1d)+                          >> $*.lnk
	echo $(OBJ1e)+                          >> $*.lnk
	echo $(OBJ1f)+                          >> $*.lnk
	echo $(OBJ1g)+                          >> $*.lnk
	echo $(OBJ2a)+                          >> $*.lnk
	echo $(OBJ2b)+                          >> $*.lnk
	echo $(OBJ2c)+                          >> $*.lnk
	echo $(OBJ2d)+                          >> $*.lnk
	echo $(OBJ2e)+                          >> $*.lnk
	echo $(OBJ2f)$(LFLAGS)/base:$(BASE)     >> $*.lnk
	echo $*.dll                             >> $*.lnk
	echo $*                                 >> $*.lnk
	echo $(NTDLLSHELL)\lib\spwnlnd+         >> $*.lnk
	echo tdb+                               >> $*.lnk
	echo $(SNN)+                            >> $*.lnk
	echo $(LIBNT)\kernel32+                 >> $*.lnk
	echo $(LIBNT)\user32                    >> $*.lnk
	echo $*.def;                            >> $*.lnk

####################################################

test.obj : test.c

##################### INCLUDE MACROS #####################

CCH= $(TK)\mem.h $(TK)\list.h $(TK)\vec.h

##################### GENERATED SOURCE #####################

msgs2.d msgs2.h msgs2.c : msgsx.exe
	msgsx

msgsx.exe : msgsx.c
	dmc msgsx -D$(TARGET)

##################### ZIP ################################

RELEASE= sc.exe scppn.exe sppn.exe scd.dll scppnd.dll sppnd.dll

# Front end only
FRONTDSRC= ppexp.d unialpha.d entity.d html.d html.di cpp.di dmcdll.di nspace.d dinit.d \
	ini.d denum.d dinline.d loadline.d parser.d scopeh.d dtoken.d adl.d utf.di utf.d \
	objrecor.d tytostr.d filename.d precomp.d pseudo.d rtti.d eh.d exp.d \
	dstruct.d func.d dcpp.d dtemplate.d dexp2.d castab.d dgetcmd.d htod.d \
	dnwc.d dblklst.d dph.d dpragma.d phstring.di phstring.d xtoken.d rawstring.di rawstring.d \
	dscope.d dmsc.d dutil.d diasm.d dfile.d dspeller.di dspeller.d err.d page.di stubobj.d \
	poptelem.d

HTODSRC= ppexp.d unialpha.d entity.d cpp.di dmcdll.di nspace.d dinit.d \
	ini.d denum.d dinline.d loadline.d parser.d scopeh.d dtoken.d adl.d utf.d \
	objrecor.d tytostr.d filename.d precomp.d pseudo.d rtti.d eh.d exp.d \
	dstruct.d func.d dcpp.d dtemplate.d dexp2.d dgetcmd.d htod.d \
	dnwc.d dblklst.d dph.d dpragma.d phstring.d xtoken.d rawstring.d \
	dscope.d dmsc.d dutil.d diasm.d dfile.d err.d page.di stubobj.d \
	poptelem.d

FRONTSRC= dmcdll.h scdll.h \
	tdb.h \
	\
	errmsgs2.c tdbx.c \
	msgsx.c \
	trace.c dmcdll.c \
	tk.c

# Back end
BACKDSRC= bcomplex.d cc.d cdef.d cgcv.d code.d code_x86.d cv4.d \
	dwarf.d dwarf2.d el.d exh.d global.d iasm.d mach.d md5.di \
	mscoff.d obj.d oper.d rtlsym.d ty.d type.d xmm.d \
	divcoeff.d dvec.d dlist.d filespec.d evalu8.d go.d gsroa.d \
	glocal.d goh.d md5.d mem.d varstats.di aarray.d

BACKSRC= \
	code_stub.h \
	\
	platform_stub.c \
	blockopt.d cg.d cg87.d cgxmm.d cgcse.d \
	cgcod.d cgcs.d dcgcv.d cgelem.d cgen.d cgobj.d \
	cgreg.d var.d compress.d codebuilder.d \
	cgsched.d cod1.d cod2.d cod3.d cod4.d cod5.d \
	dcode.d symbol.d symtab.d debugprint.d dt.d ee.d elem.d elpicpie.d \
	fp.c gflow.d gdag.d \
	gother.d gloop.d newman.d \
	nteh.d os.c out.d outbuf.d ptrntab.d drtlsym.d \
	dtype.d \
	elfobj.d aarray.d barray.d \
	dwarfdbginf.d machobj.d\
	strtold.c \
	ph2.d util2.d \
	mscoffobj.d pdata.d cv8.d backconfig.d \
	divcoeff.d dwarfeh.d dvarstats.d melf.d errors.di \
	backend.txt

SRC= $(FRONTSRC) $(FRONTDSRC) $(BACKSRC) $(BACKDSRC)

TKSRC= $(TK)\mem.h $(TK)\list.h $(TK)\vec.h $(TK)\filespec.h \
	$(TK)\mem.c

SHELLSRC= $(NTDLLSHELL)\include\dllrun.h $(NTDLLSHELL)\include\network.h \
		$(NTDLLSHELL)\include\nsidde.h $(NTDLLSHELL)\include\netspawn.h

LIBSRC= $(NTDLLSHELL)\lib\spwnlnd.lib $(NTDLLSHELL)\lib\netsploc.obj

BUILDSRC= sppn.tra sccn.tra htodn.tra scppn.tra makefile posix.mak

LICENSE= license.txt gpl.txt

VERSION=857

htod.exe : $(HTODSRC) msgs2.d dspeller.obj castab.d
	$(DMD) $(DFLAGS) $(HTODSRC) msgs2.d dspeller.obj -J.

zip:
	del dm$(VERSION).zip
	zip32 -u dm$(VERSION) $(RELEASE)

zipsrc:
	del dm$(VERSION)src.zip
	zip32 -u dm$(VERSION)src $(SRC) tdb.lib $(LICENSE) backend.txt
	zip32 -u dm$(VERSION)src $(TKSRC)
	zip32 -u dm$(VERSION)src $(SHELLSRC)
	zip32 -u dm$(VERSION)src $(LIBSRC)
	zip32 -u dm$(VERSION)src $(BUILDSRC)

detab:
	detab $(SRC) $(TKSRC) $(SHELLSRC)

tolf:
	tolf $(SRC) $(TKSRC) $(SHELLSRC) $(BUILDSRC) $(LICENSE)

install:
	cp $(SRC) $(BUILDSRC) tdb.lib $(LICENSE) \dm\src\dmc
	cp $(TKSRC) \dm\src\dmc\tk
	cp $(SHELLSRC) \dm\src\dmc\ntdllshell\include
	cp $(LIBSRC) \dm\src\dmc\ntdllshell\lib

installbin:
	cp scppn.exe sppn.exe scppnd.dll sppnd.dll htodn.exe \dm\bin

git: detab tolf $(BUILDSRC)
	$(SCP) $(SRC) $(BUILDSRC) tdb.lib $(LICENSE) $(SCPDIR)/Compiler/dm/src/dmc/
	$(SCP) $(TKSRC) $(SCPDIR)/Compiler/dm/src/dmc/tk
	$(SCP) $(SHELLSRC) $(SCPDIR)/Compiler/dm/src/dmc/ntdllshell/include
	$(SCP) $(LIBSRC) $(SCPDIR)/Compiler/dm/src/dmc/ntdllshell/lib

gitback: detab tolf $(BACKSRC)
	$(SCP) $(BACKSRC) walter@mercury:forks/dmd/src/backend/


##################### SPECIAL BUILDS #####################

os.obj : os.c
	$(CC) -c $(CFLAGS) os.c

strtold.obj : strtold.c
	$(CC) -c -o strtold.c

adl.obj : adl.d
	$(DMD) -c $(DFLAGS) adl.d

backconfig.obj : backconfig.d
	$(DMD) -c $(DFLAGS) backconfig.d

barray.obj : barray.d
	$(DMD) -c $(DFLAGS) barray.d

bcomplex.obj : bcomplex.d
	$(DMD) -c $(DFLAGS) bcomplex.d

blockopt.obj : blockopt.d
	$(DMD) -c $(DFLAGS) blockopt.d

dblklst.obj : dblklst.d
	$(DMD) -c $(DFLAGS) dblklst.d

cgcs.obj : cgcs.d
	$(DMD) -c $(DFLAGS) cgcs.d

dcode.obj : dcode.d
	$(DMD) -c $(DFLAGS) dcode.d

cgcod.obj : cgcod.d
	$(DMD) -c $(DFLAGS) -J. cgcod.d

cod1.obj : cod1.d
	$(DMD) -c $(DFLAGS) cod1.d

cod2.obj : cod2.d
	$(DMD) -c $(DFLAGS) cod2.d

cod3.obj : cod3.d
	$(DMD) -c $(DFLAGS) cod3.d

cod4.obj : cod4.d
	$(DMD) -c $(DFLAGS) cod4.d

cod5.obj : cod5.d
	$(DMD) -c $(DFLAGS) cod5.d

codebuilder.obj : codebuilder.d
	$(DMD) -c $(DFLAGS) -J. codebuilder.d

code_x86.obj : code_x86.d
	$(DMD) -c $(DFLAGS) code_x86.d

cg.obj : cg.d
	$(DMD) -c $(DFLAGS) -J. cg.d

cg87.obj : cg87.d
	$(DMD) -c $(DFLAGS) -J. cg87.d

cgcse.obj : cgcse.d
	$(DMD) -c $(DFLAGS) cgcse.d

cgelem.obj : cgelem.d
	$(DMD) -c $(DFLAGS) -J. cgelem.d

cgen.obj : cgen.d
	$(DMD) -c $(DFLAGS) cgen.d

cgobj.obj : cgobj.d code.d
	$(DMD) -c $(DFLAGS) -J. cgobj.d code.d

mscoffobj.obj : mscoffobj.d
	$(DMD) -c $(DFLAGS) -J. mscoffobj.d

dvarstats.obj : dvarstats.d
	$(DMD) -c $(DFLAGS) -J. dvarstats.d

cgreg.obj : cgreg.d
	$(DMD) -c $(DFLAGS) cgreg.d

cgsched.obj : cgsched.d
	$(DMD) -c $(DFLAGS) cgsched.d

cgxmm.obj : cgxmm.d
	$(DMD) -c $(DFLAGS) cgxmm.d

compress.obj : compress.d
	$(DMD) -c $(DFLAGS) compress.d

cv8.obj : cv8.d
	$(DMD) -c $(DFLAGS) cv8.d

dcgcv.obj : dcgcv.d
	$(DMD) -c $(DFLAGS) dcgcv.d

dcpp.obj : msgs2.d dcpp.d
	$(DMD) -c $(DFLAGS) dcpp.d

debugprint.obj : debugprint.d
	$(DMD) -c $(DFLAGS) debugprint.d

denum.obj : denum.d
	$(DMD) -c $(DFLAGS) denum.d

dt.obj : dt.d
	$(DMD) -c $(DFLAGS) dt.d

dwarfdbginf.obj : dwarfdbginf.d
	$(DMD) -c $(DFLAGS) dwarfdbginf.d

err.obj : err.d
	$(DMD) -c $(DFLAGS) err.d

dexp2.obj : dexp2.d castab.d
	$(DMD) -c $(DFLAGS) -J. dexp2.d

dfile.obj : dfile.d
	$(DMD) -c $(DFLAGS) -J. dfile.d

dgetcmd.obj : dgetcmd.d
	$(DMD) -c $(DFLAGS) dgetcmd.d

diasm.obj : diasm.d
	$(DMD) -c $(DFLAGS) diasm.d

dmsc.obj : dmsc.d
	$(DMD) -c $(DFLAGS) dmsc.d

dnwc.obj : dnwc.d
	$(DMD) -c $(DFLAGS) dnwc.d

dph.obj : dph.d page.di
	$(DMD) -c $(DFLAGS) dph.d

dpragma.obj : dpragma.d
	$(DMD) -c $(DFLAGS) dpragma.d

dscope.obj : dscope.d
	$(DMD) -c $(DFLAGS) dscope.d

dspeller.obj : dspeller.d
	$(DMD) -c $(DFLAGS) dspeller.d

dtemplate.obj : dtemplate.d
	$(DMD) -c $(DFLAGS) dtemplate.d

dtype.obj : dtype.d
	$(DMD) -c $(DFLAGS) dtype.d

dwarfeh.obj : dwarfeh.d
	$(DMD) -c $(DFLAGS) dwarfeh.d

elem.obj : elem.d
	$(DMD) -c $(DFLAGS) elem.d

elpicpie.obj : elpicpie.d
	$(DMD) -c $(DFLAGS) elpicpie.d

evalu8.obj : evalu8.d
	$(DMD) -c $(DFLAGS) evalu8.d

filespec.obj : filespec.d
	$(DMD) -c $(DFLAGS) filespec.d

func.obj : func.d
	$(DMD) -c $(DFLAGS) func.d

gdag.obj : gdag.d
	$(DMD) -c $(DFLAGS) gdag.d

gflow.obj : gflow.d
	$(DMD) -c $(DFLAGS) gflow.d

glocal.obj : glocal.d
	$(DMD) -c $(DFLAGS) glocal.d

global.obj : global.d
	$(DMD) -c $(DFLAGS) global.d

gloop.obj : gloop.d
	$(DMD) -c $(DFLAGS) gloop.d

go.obj : go.d
	$(DMD) -c $(DFLAGS) go.d

goh.obj : goh.d
	$(DMD) -c $(DFLAGS) goh.d

gother.obj : gother.d
	$(DMD) -c $(DFLAGS) gother.d

gsroa.obj : gsroa.d
	$(DMD) -c $(DFLAGS) gsroa.d

ini.obj : ini.d
	$(DMD) -c $(DFLAGS) ini.d

dinit.obj : dinit.d
	$(DMD) -c $(DFLAGS) dinit.d

nspace.obj : nspace.d
	$(DMD) -c $(DFLAGS) nspace.d

divcoeff.obj : divcoeff.d
	$(DMD) -c $(DFLAGS) divcoeff.d

dvec.obj : dvec.d
	$(DMD) -c $(DFLAGS) dvec.d

loadline.obj : loadline.d msgs2.d
	$(DMD) -c $(DFLAGS) loadline.d

dinline.obj : dinline.d
	$(DMD) -c $(DFLAGS) dinline.d

html.obj : html.d
	$(DMD) -c $(DFLAGS) html.d

htod.obj : htod.d
	$(DMD) -c $(DFLAGS) htod.d

oper.obj : oper.d
	$(DMD) -c $(DFLAGS) oper.d

parser.obj : parser.d
	$(DMD) -c $(DFLAGS) parser.d

poptelem.obj : poptelem.d
	$(DMD) -c $(DFLAGS) poptelem.d

precomp.obj : precomp.d
	$(DMD) -c $(DFLAGS) precomp.d

dlist.obj : dlist.d
	$(DMD) -c $(DFLAGS) dlist.d

dstruct.obj : dstruct.d
	$(DMD) -c $(DFLAGS) dstruct.d

dth.obj : dt.d
	$(DMD) -c $(DFLAGS) dt.d -ofdth.obj

dtoken.obj : dtoken.d
	$(DMD) -c $(DFLAGS) dtoken.d

xtoken.obj : xtoken.d
	$(DMD) -c $(DFLAGS) xtoken.d

dutil.obj : dutil.d
	$(DMD) -c $(DFLAGS) dutil.d

ee.obj : ee.d
	$(DMD) -c $(DFLAGS) ee.d

eh.obj : msgs2.d eh.d
	$(DMD) -c $(DFLAGS) eh.d

entity.obj : entity.d
	$(DMD) -c $(DFLAGS) entity.d

exp.obj : exp.d
	$(DMD) -c $(DFLAGS) exp.d

filename.obj : filename.d
	$(DMD) -c $(DFLAGS) filename.d

newman.obj : newman.d
	$(DMD) -c $(DFLAGS) newman.d

nteh.obj : nteh.d
	$(DMD) -c $(DFLAGS) nteh.d

obj.obj : obj.d
	$(DMD) -c $(DFLAGS) obj.d

objrecor.obj : objrecor.d
	$(DMD) -c $(DFLAGS) objrecor.d

out.obj : out.d
	$(DMD) -c $(DFLAGS) out.d

outbuf.obj : outbuf.d
	$(DMD) -c $(DFLAGS) outbuf.d

pdata.obj : pdata.d
	$(DMD) -c $(DFLAGS) pdata.d

phstring.obj : phstring.d
	$(DMD) -c $(DFLAGS) phstring.d

ppexp.obj : ppexp.d
	$(DMD) -c $(DFLAGS) ppexp.d

pseudo.obj : pseudo.d
	$(DMD) -c $(DFLAGS) pseudo.d

ptrntab.obj : ptrntab.d
	$(DMD) -c $(DFLAGS) ptrntab.d

rawstring.obj : rawstring.d
	$(DMD) -c $(DFLAGS) rawstring.d

drtlsym.obj : rtlsym.d drtlsym.d
	$(DMD) -c $(DFLAGS) drtlsym.d

rtti.obj : rtti.d
	$(DMD) -c $(DFLAGS) rtti.d

stubobj.obj : stubobj.d
	$(DMD) -c $(DFLAGS) stubobj.d

symbol.obj : symbol.d
	$(DMD) -c $(DFLAGS) symbol.d

symtab.obj : symtab.d
	$(DMD) -c $(DFLAGS) symtab.d

tytostr.obj : tytostr.d
	$(DMD) -c $(DFLAGS) tytostr.d

unialpha.obj : unialpha.d
	$(DMD) -c $(DFLAGS) unialpha.d

utf.obj : utf.d
	$(DMD) -c $(DFLAGS) utf.d

var.obj : var.d
	$(DMD) -c $(DFLAGS) var.d

################# Source file dependencies ###############

DEP= msgs2.h msgs2.c

#backconfig.obj : $(DEP) $(backconfig.dep) backconfig.c
#cgreg.obj : $(DEP) $(cgreg.dep) cgreg.c
#cod1.obj : $(DEP) $(cod1.dep) cod1.c
#cod2.obj : $(DEP) $(cod2.dep) cod2.c
#cod3.obj : $(DEP) $(cod3.dep) cod3.c
#cod4.obj : $(DEP) $(cod4.dep) cod4.c
#code.obj : $(DEP) $(code.dep) code.c
#cv8.obj : $(DEP) $(cv8.dep) cv8.c
#debug.obj : $(DEP) $(debug.dep) debug.c
#dt.obj : $(DEP) $(dt.dep) dt.c
#dwarf.obj : $(DEP) $(dwarf.dep) dwarf.h dwarf.c
#dwarfeh.obj : $(DEP) $(dwarfeh.dep) dwarf.h dwarfeh.c
#cel.obj : $(DEP) $(cel.dep) cel.c
errmsgs2.obj : $(DEP) $(errmsgs2.dep) errmsgs2.c msgs2.c
fp.obj : $(DEP) $(fp.dep) fp.c
#out.obj : $(DEP) $(out.dep) out.c
#pdata.obj : $(DEP) $(pdata.dep) pdata.c
#ptrntab.obj : $(DEP) $(ptrntab.dep) iasm.h ptrntab.c
tk.obj : $(DEP) $(tk.dep) tk.c
#type.obj : $(DEP) $(type.dep) type.c
#cgcv.obj : $(DEP) $(cgcv.dep) cgcv.c
dmcdll.obj : $(DEP) $(dmcdll.dep) dmcdll.c
#gsroa.obj : $(DEP) $(gsroa.dep) gsroa.c
#go.obj : $(DEP) $(go.dep) go.c
#gflow.obj : $(DEP) $(gflow.dep) gflow.c
#cgcod.obj : $(DEP) $(cgcod.dep) cgcod.c
#cgen.obj : $(DEP) $(cgen.dep) cgen.c
#rtlsym.obj : $(DEP) $(rtlsym.dep) rtlsym.c
#sizecheck.obj : $(DEP) $(sizecheck.dep) sizecheck.c
#symbol.obj : $(DEP) $(symbol.dep) symbol.c
#cg87.obj : $(DEP) $(cg87.dep) cg87.c
#cgxmm.obj : $(DEP) xmm.h $(cgxmm.dep) cgxmm.c
#outbuf.obj : $(DEP) $(outbuf.dep) outbuf.c
#newman.obj : $(DEP) $(newman.dep) newman.c
tdbx.obj : $(DEP) $(tdbx.dep) tdbx.c
#var.obj : $(DEP) $(var.dep) var.c

################### Utilities ################

clean:
	del *.obj
	del *.lnk
	del *.sym
	del *.dep
	del *.def
#	del *.bak
	del msgsx.exe
	del *.map
	del *.tmp
	del *.lst
	del *.exp
	del *.dbg
	del *.res
	del msgs.c msgs2.d msgs2.h msgs2.c

###################################
