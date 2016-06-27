!IF "$(PLATFORM)"=="X64"
OUTDIR=.\bin64
!ELSE
OUTDIR=.\bin
!ENDIF

CC=cl
LINKER=link
RM=del /q

TARGET=child.exe

OBJS=$(OUTDIR)\main.obj\

LIBS=\

CFLAGS=\
    /nologo\
    /Zi\
    /c\
    /Fo"$(OUTDIR)\\"\
    /Fd"$(OUTDIR)\\"\
    /DUNICODE\
    /O2\
    /EHsc\
    /W4\

LFLAGS=\
    /NOLOGO\
    /DEBUG\
    /SUBSYSTEM:CONSOLE\

all: clean $(OUTDIR)\$(TARGET)

clean:
    -@if not exist $(OUTDIR) md $(OUTDIR)
    @$(RM) /Q $(OUTDIR)\* 2>nul

$(OUTDIR)\$(TARGET): $(OBJS)
    $(LINKER) $(LFLAGS) $(LIBS) /PDB:"$(@R).pdb" /OUT:"$(OUTDIR)\$(TARGET)" $**

.cpp{$(OUTDIR)}.obj:
    $(CC) $(CFLAGS) $<