!IF "$(PLATFORM)"=="X64"
OUTDIR=.\bin64
ARCH=amd64
!ELSE
OUTDIR=.\bin
ARCH=x86
!ENDIF

CC=cl
LINKER=link
RM=del /q

TARGET=sudo.exe

OBJS=$(OUTDIR)\main.obj\

LIBS=\
!IF "$(USE_USER32)"=="1"
     user32.lib\
!ENDIF

CFLAGS=\
    /nologo\
    /Zi\
    /c\
    /Fo"$(OUTDIR)\\"\
    /Fd"$(OUTDIR)\\"\
    /DUNICODE\
    /D_UNICODE\
!IF "$(USE_USER32)"=="1"
    /DUSE_USER32\
!ENDIF
    /O2\
    /EHsc\
    /W4\

LFLAGS=\
    /NOLOGO\
    /DEBUG\
    /SUBSYSTEM:WINDOWS\

all: clean $(OUTDIR)\$(TARGET)

clean:
    -@if not exist $(OUTDIR) md $(OUTDIR)
    @$(RM) /Q $(OUTDIR)\* 2>nul

$(OUTDIR)\$(TARGET): $(OBJS)
    $(LINKER) $(LFLAGS) $(LIBS) /PDB:"$(@R).pdb" /OUT:"$(OUTDIR)\$(TARGET)" $**
    MT -nologo -manifest manifest.xml\
       -identity:"sudo, type=win32, version=1.0.0.0, processorArchitecture=$(ARCH)"\
       -outputresource:$@;1

.cpp{$(OUTDIR)}.obj:
    $(CC) $(CFLAGS) $<