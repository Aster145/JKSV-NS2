#---------------------------------------------------------------------------------
.SUFFIXES:
#---------------------------------------------------------------------------------

ifeq ($(strip $(DEVKITPRO)),)
$(error "Please set DEVKITPRO in your environment. export DEVKITPRO=<path to>/devkitpro")
endif

TOPDIR ?= $(CURDIR)
include $(DEVKITPRO)/libounce/switch2_rules

ENABLE_CURL ?= 1
ENABLE_REMOTE ?= $(ENABLE_CURL)
ifneq ($(ENABLE_CURL),0)
ifneq ($(ENABLE_CURL),1)
$(error ENABLE_CURL must be 0 or 1)
endif
endif
ifneq ($(ENABLE_REMOTE),0)
ifneq ($(ENABLE_REMOTE),1)
$(error ENABLE_REMOTE must be 0 or 1)
endif
endif
ifeq ($(ENABLE_CURL)$(ENABLE_REMOTE),01)
$(error ENABLE_REMOTE=1 requires ENABLE_CURL=1)
endif
export ENABLE_CURL ENABLE_REMOTE
CPPFLAGS += -DJKSV_ENABLE_CURL=$(ENABLE_CURL) -DJKSV_ENABLE_REMOTE=$(ENABLE_REMOTE)

#---------------------------------------------------------------------------------
# TARGET is the name of the output
# BUILD is the directory where object files & intermediate files will be placed
# SOURCES is a list of directories containing source code
# DATA is a list of directories containing data files
# INCLUDES is a list of directories containing header files
# EXEFS_SRC is the optional input directory containing data copied into exefs, if anything this normally should only contain "main.npdm".
# ROMFS is the directory containing data to be added to RomFS, relative to the Makefile (Optional)
#
# NO_ICON: if set to anything, do not use icon.
# NO_NACP: if set to anything, no .nacp file is generated.
# APP_TITLE is the name of the app stored in the .nacp file (Optional)
# APP_AUTHOR is the author of the app stored in the .nacp file (Optional)
# APP_VERSION is the version of the app stored in the .nacp file (Optional)
# APP_TITLEID is the titleID of the app stored in the .nacp file (Optional)
# ICON is the filename of the icon (.jpg), relative to the project folder.
#   If not set, it attempts to use one of the following (in this order):
#     - <Project name>.jpg
#     - icon.jpg
#     - <libnx folder>/default_icon.jpg
#---------------------------------------------------------------------------------
TARGET		:=	JKSV-NS2
BUILD		:=	build
SOURCES		:=	source source/appstates source/config source/data source/fs \
				source/keyboard source/logging source/strings source/sys \
				source/tasks source/ui
ifeq ($(ENABLE_CURL),1)
SOURCES += source/curl
endif
ifeq ($(ENABLE_REMOTE),1)
SOURCES += source/remote
endif
DATA		:=	data
INCLUDES	:=	include ./Libraries/FsLib/Switch/FsLib/include ./Libraries/SDLLib/SDL/include
EXEFS_SRC	:=	exefs_src
APP_TITLE   :=  JKSV-NS2
APP_AUTHOR  :=  果冻扭扭
APP_VERSION :=  1.0.0
ROMFS	    :=	romfs
ICON		:=	icon.jpg

#---------------------------------------------------------------------------------
# options for code generation
#---------------------------------------------------------------------------------
ARCH	:=	$(SWITCH2_CPU) -mtp=soft -fPIE

# NOTE: For some reason, devkitpro no longer has freetype-config included? Also, using pkg-config causes conflicts with
# my local pkg-config and I don't feel like dealing with CMake for all of this right now.
# SDL2 is shared with Switch 1, but its helper's CPU flags must not override SWITCH2_CPU.
SDL_CFLAGS := $(filter-out -march=% -mtune=% -mcpu=%,$(shell $(PORTLIBS)/bin/sdl2-config --cflags))
# Retain the allocator used in the successful Switch 2 diagnostic builds.
SDL_LIBS := $(filter-out -lnx -L$(DEVKITPRO)/libnx/lib,$(shell $(PORTLIBS)/bin/sdl2-config --libs))
CURL_CFLAGS :=
CURL_LIBS :=
REMOTE_LIBS :=
ifeq ($(ENABLE_CURL),1)
CURL_CFLAGS := `$(PORTLIBS)/bin/curl-config --cflags`
CURL_LIBS := $(filter-out -lnx -L$(DEVKITPRO)/libnx/lib,$(shell $(PORTLIBS)/bin/curl-config --libs))
endif
ifeq ($(ENABLE_REMOTE),1)
REMOTE_LIBS := -ltinyxml2
endif

CFLAGS	:=	$(INCLUDE) -D__SWITCH__ $(SDL_CFLAGS) $(CURL_CFLAGS)\
			-g -Wall -O3 -Os -ffunction-sections -ffast-math -fmax-errors=1 \
			-I$(PORTLIBS)/include/freetype2 $(ARCH) $(DEFINES)

CXXFLAGS:= $(CFLAGS) -fno-rtti -fno-exceptions -std=c++23

ASFLAGS	:=	-g $(ARCH)
LDFLAGS	=	-specs=$(LIBNX)/switch.specs -g $(ARCH) -Wl,-Map,$(notdir $*.map)

LIBS	:=	../Libraries/FsLib/Switch/FsLib/lib/libFsLib.a ../Libraries/SDLLib/SDL/lib/libSDL.a \
			$(SDL_LIBS) -lfreetype -lharfbuzz $(CURL_LIBS) -lSDL2_image  \
			-lwebp -lpng -ljpeg -lz -lminizip -ljson-c $(REMOTE_LIBS) -lounce_core -lbz2 -lz

#---------------------------------------------------------------------------------
# list of directories containing libraries, this must be the top level containing
# include and lib
#---------------------------------------------------------------------------------
LIBDIRS	:= $(LIBNX) $(PORTLIBS)


#---------------------------------------------------------------------------------
# no real need to edit anything past this point unless you need to add additional
# rules for different file extensions
#---------------------------------------------------------------------------------
ifneq ($(BUILD),$(notdir $(CURDIR)))
#---------------------------------------------------------------------------------

export OUTPUT	:=	$(CURDIR)/$(TARGET)
export TOPDIR	:=	$(CURDIR)

export VPATH	:=	$(foreach dir,$(SOURCES),$(CURDIR)/$(dir)) \
			$(foreach dir,$(DATA),$(CURDIR)/$(dir))

export DEPSDIR	:=	$(CURDIR)/$(BUILD)

CFILES		:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))
CPPFILES	:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.cpp)))
SFILES		:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.s)))
BINFILES	:=	$(foreach dir,$(DATA),$(notdir $(wildcard $(dir)/*.*)))

#---------------------------------------------------------------------------------
# use CXX for linking C++ projects, CC for standard C
#---------------------------------------------------------------------------------
ifeq ($(strip $(CPPFILES)),)
#---------------------------------------------------------------------------------
	export LD	:=	$(CC)
#---------------------------------------------------------------------------------
else
#---------------------------------------------------------------------------------
	export LD	:=	$(CXX)
#---------------------------------------------------------------------------------
endif
#---------------------------------------------------------------------------------

export OFILES_BIN	:=	$(addsuffix .o,$(BINFILES))
export OFILES_SRC	:=	$(CPPFILES:.cpp=.o) $(CFILES:.c=.o) $(SFILES:.s=.o)
export OFILES 	:=	$(OFILES_BIN) $(OFILES_SRC)
export HFILES_BIN	:=	$(addsuffix .h,$(subst .,_,$(BINFILES)))

export INCLUDE	:=	$(foreach dir,$(INCLUDES),-I$(CURDIR)/$(dir)) \
			$(foreach dir,$(LIBDIRS),-I$(dir)/include) \
			-I$(CURDIR)/$(BUILD)

export LIBPATHS	:=	$(foreach dir,$(LIBDIRS),-L$(dir)/lib)

export BUILD_EXEFS_SRC := $(TOPDIR)/$(EXEFS_SRC)

ifeq ($(strip $(ICON)),)
	icons := $(wildcard *.jpg)
	ifneq (,$(findstring $(TARGET).jpg,$(icons)))
		export APP_ICON := $(TOPDIR)/$(TARGET).jpg
	else
		ifneq (,$(findstring icon.jpg,$(icons)))
			export APP_ICON := $(TOPDIR)/icon.jpg
		endif
	endif
else
	export APP_ICON := $(TOPDIR)/$(ICON)
endif

ifeq ($(strip $(NO_ICON)),)
	export NROFLAGS += --icon=$(APP_ICON)
endif

ifeq ($(strip $(NO_NACP)),)
	export NROFLAGS += --nacp=$(CURDIR)/$(TARGET).nacp
endif

ifneq ($(APP_TITLEID),)
	export NACPFLAGS += --titleid=$(APP_TITLEID)
endif

ifneq ($(ROMFS),)
	export NROFLAGS += --romfsdir=$(CURDIR)/$(ROMFS)
endif

.PHONY: $(BUILD) FsLib SDLLib Assets clean all

#---------------------------------------------------------------------------------
all: FsLib SDLLib Assets $(BUILD)

$(BUILD): FsLib SDLLib Assets
	@[ -d $@ ] || mkdir -p $@
	@printf '%s\n' 'ENABLE_CURL=$(ENABLE_CURL)' 'ENABLE_REMOTE=$(ENABLE_REMOTE)' > $(BUILD)/build-options.tmp
	@cmp -s $(BUILD)/build-options.tmp $(BUILD)/build-options.txt || cp $(BUILD)/build-options.tmp $(BUILD)/build-options.txt
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile

#---------------------------------------------------------------------------------

FsLib:
	@$(MAKE) -C ./Libraries/FsLib/Switch/FsLib/ TARGET=FsLib

#---------------------------------------------------------------------------------

SDLLib:
	@$(MAKE) -C ./Libraries/SDLLib/SDL/ TARGET=SDL

#---------------------------------------------------------------------------------

Assets:
	@echo python script
	@python ./Assets/compress_assets.py

#---------------------------------------------------------------------------------

clean:
	@echo clean ...
	@$(MAKE) -C ./Libraries/FsLib/Switch/FsLib clean
	@$(MAKE) -C ./Libraries/SDLLib/SDL clean
	@rm -fr $(BUILD) $(TARGET).pfs0 $(TARGET).nso $(TARGET).nro $(TARGET).nacp $(TARGET).elf
	@rm -r ./romfs/Text

#---------------------------------------------------------------------------------
send: $(BUILD)
	@nxlink $(TARGET).nro

#---------------------------------------------------------------------------------

debug: $(BUILD)
	@nxlink -s $(TARGET).nro


else
.PHONY:	all

DEPENDS	:=	$(OFILES:.o=.d)

#---------------------------------------------------------------------------------
# main targets
#---------------------------------------------------------------------------------
all	:	$(OUTPUT).pfs0 $(OUTPUT).nro

$(OUTPUT).pfs0	:	$(OUTPUT).nso

$(OUTPUT).nso	:	$(OUTPUT).elf

ifeq ($(strip $(NO_NACP)),)
$(OUTPUT).nro	:	$(OUTPUT).elf $(OUTPUT).nacp $(APP_ICON)

# Windows nacptool may encode non-ASCII argv using the local ANSI code page.
$(OUTPUT).nacp: $(MAKEFILE_LIST) $(TOPDIR)/Assets/create_nacp.py
	@python "$(TOPDIR)/Assets/create_nacp.py" "$(APP_TITLE)" "$(APP_AUTHOR)" "$(APP_VERSION)" "$@" $(NACPFLAGS)
	@echo built ... $(notdir $@)
else
$(OUTPUT).nro	:	$(OUTPUT).elf
endif

$(OUTPUT).elf	:	$(OFILES)

$(OFILES_SRC)	: $(HFILES_BIN)
$(OFILES_SRC)	: $(TOPDIR)/Makefile $(TOPDIR)/$(BUILD)/build-options.txt $(LIBNX)/switch2_rules

#---------------------------------------------------------------------------------
# you need a rule like this for each extension you use as binary data
#---------------------------------------------------------------------------------
%.bin.o	%_bin.h :	%.bin
#---------------------------------------------------------------------------------
	@echo $(notdir $<)
	@$(bin2o)

-include $(DEPENDS)

#---------------------------------------------------------------------------------------
endif
#---------------------------------------------------------------------------------------
