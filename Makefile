#---------------------------------------------------------------------------------
.SUFFIXES:
#---------------------------------------------------------------------------------

# Locate devkitPro. Default to the standard install location and repair a
# missing/incorrect DEVKITARM (when DEVKITPRO is unset it resolves to
# "/devkitARM"), so a plain `make` works without pre-exporting env vars.
# base_tools then adds $(DEVKITPRO)/tools/bin + devkitARM/bin to PATH for us.
ifeq ($(strip $(DEVKITPRO)),)
export DEVKITPRO := /opt/devkitpro
endif

ifeq ($(wildcard $(DEVKITARM)/ds_rules),)
export DEVKITARM := $(DEVKITPRO)/devkitARM
endif

ifeq ($(wildcard $(DEVKITARM)/ds_rules),)
$(error Could not find $(DEVKITARM)/ds_rules. Install devkitPro, or set DEVKITPRO/DEVKITARM in your environment)
endif

# Banner text baked into the ROM (shown by launchers like TWiLight Menu++)
GAME_TITLE     := Connections
GAME_SUBTITLE1 := Not a NYT Game
# 4-character game code and <=12-char internal title used for the DSi build
GAME_CODE       := CNDS
GAME_TITLE_SHORT := CONNECTIONS

include $(DEVKITARM)/ds_rules

#---------------------------------------------------------------------------------
# TARGET    is the name of the output
# BUILD     is the directory where object files & intermediate files will be placed
# SOURCES   is a list of directories containing source code
# INCLUDES  is a list of directories containing extra header files
# DATA      is a list of directories containing binary files embedded using bin2o
# GRAPHICS  is a list of directories containing image files to be converted with grit
# AUDIO     is a list of directories containing audio to be converted by maxmod
# ICON      is the image used to create the game icon
#---------------------------------------------------------------------------------
TARGET   := connections-ds
BUILD    := build
SOURCES  := source
INCLUDES := include
DATA     := data
GRAPHICS := gfx
AUDIO    :=
ICON     := icon.bmp

# no NitroFS: assets are embedded (data/) and puzzles are fetched or bundled
NITRO    :=

#---------------------------------------------------------------------------------
# options for code generation
#---------------------------------------------------------------------------------
ARCH := -march=armv5te -mtune=arm946e-s

CFLAGS := -g -Wall -O2 -ffunction-sections -fdata-sections\
 $(ARCH) $(INCLUDE) -DARM9
CXXFLAGS := $(CFLAGS) -fno-rtti -fno-exceptions -std=gnu++17
ASFLAGS := -g $(ARCH)
LDFLAGS = -specs=ds_arm9.specs -g $(ARCH) -Wl,-Map,$(notdir $*.map)

#---------------------------------------------------------------------------------
# any extra libraries we wish to link with the project (order is important)
#   -lfat      : SD card access for save data / share.txt
#   -ldswifi9  : Wi-Fi (DS Mitsumi + DSi Atheros/WPA2 in libnds v2)
#   -lnds9     : libnds
#---------------------------------------------------------------------------------
LIBS := -lfat -ldswifi9 -lnds9

# automagically add maxmod library
ifneq ($(strip $(AUDIO)),)
LIBS := -lmm9 $(LIBS)
endif

#---------------------------------------------------------------------------------
# list of directories containing libraries, this must be the top level containing
# include and lib
#---------------------------------------------------------------------------------
LIBDIRS := $(LIBNDS) $(PORTLIBS)

#---------------------------------------------------------------------------------
# no real need to edit anything past this point unless you need to add additional
# rules for different file extensions
#---------------------------------------------------------------------------------
ifneq ($(BUILD),$(notdir $(CURDIR)))
#---------------------------------------------------------------------------------

export OUTPUT := $(CURDIR)/$(TARGET)

export VPATH := $(CURDIR)/$(subst /,,$(dir $(ICON)))\
 $(foreach dir,$(SOURCES),$(CURDIR)/$(dir))\
 $(foreach dir,$(DATA),$(CURDIR)/$(dir))\
 $(foreach dir,$(GRAPHICS),$(CURDIR)/$(dir))

export DEPSDIR := $(CURDIR)/$(BUILD)

CFILES   := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))
CPPFILES := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.cpp)))
SFILES   := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.s)))

# Grit image assets: gfx/<name>.png (+ matching .grit flags) are converted to a
# GRF bitmap and embedded via bin2o. Enumerated explicitly so stray files in
# gfx/ never enter the build. Regenerate the PNGs with `python3 tools/gen_gfx.py`.
GFXASSETS := toolInfo toolSyncOn toolSyncOff toolShuffleOn toolShuffleOff \
 deselectActive deselectDisabled submitActive submitDisabled \
 howtoTop howtoBottom header stats share
GRFFILES := $(addsuffix .grf,$(GFXASSETS))

# Only embed formats with bin2o rules below. Reference assets (e.g. *.nftr) stay
# in data/ for inspection but are not linked into the ROM.
BINFILES := $(foreach dir,$(DATA),$(notdir $(wildcard $(dir)/*.bin $(dir)/*.bmp))) $(GRFFILES)

#---------------------------------------------------------------------------------
# use CXX for linking C++ projects, CC for standard C
#---------------------------------------------------------------------------------
ifeq ($(strip $(CPPFILES)),)
#---------------------------------------------------------------------------------
 export LD := $(CC)
#---------------------------------------------------------------------------------
else
#---------------------------------------------------------------------------------
 export LD := $(CXX)
#---------------------------------------------------------------------------------
endif
#---------------------------------------------------------------------------------

export OFILES_BIN := $(addsuffix .o,$(BINFILES))

export OFILES_SOURCES := $(CPPFILES:.cpp=.o) $(CFILES:.c=.o) $(SFILES:.s=.o)

export OFILES := $(OFILES_BIN) $(OFILES_SOURCES)

export HFILES := $(addsuffix .h,$(subst .,_,$(BINFILES)))

export INCLUDE := $(foreach dir,$(INCLUDES),-iquote $(CURDIR)/$(dir))\
 $(foreach dir,$(LIBDIRS),-I$(dir)/include)\
 -I$(CURDIR)/$(BUILD)
export LIBPATHS := $(foreach dir,$(LIBDIRS),-L$(dir)/lib)

# Banner icon for ndstool -b. Prefer a 16-color 4bpp BMP (generated from
# icon.bmp); grit→GRF embeds a size word that current ndstool treats as pixels.
ifeq ($(strip $(ICON)),)
 icons := $(wildcard *.bmp)
 ifneq (,$(findstring $(TARGET).bmp,$(icons)))
 export GAME_ICON_SRC := $(CURDIR)/$(TARGET).bmp
 else
 ifneq (,$(findstring icon.bmp,$(icons)))
 export GAME_ICON_SRC := $(CURDIR)/icon.bmp
 endif
 endif
else
 export GAME_ICON_SRC := $(CURDIR)/$(ICON)
endif
export GAME_ICON := $(CURDIR)/$(BUILD)/icon_banner.bmp

.PHONY: $(BUILD) clean all dsi nds

#---------------------------------------------------------------------------------
all: $(BUILD)

# Keep the in-game logo in sync with the ROM banner icon.
$(CURDIR)/data/logo.bmp: $(CURDIR)/icon.bmp
	@cp -f $< $@

$(BUILD): $(CURDIR)/data/logo.bmp
	@mkdir -p $@
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile

# Convenience targets that build only one flavour
nds: $(BUILD)
dsi: $(BUILD)

#---------------------------------------------------------------------------------
clean:
	@echo clean ...
	@rm -fr $(BUILD) $(TARGET).elf $(TARGET).nds $(TARGET).dsi

#---------------------------------------------------------------------------------
else

#---------------------------------------------------------------------------------
# main targets - build both a plain .nds (flashcards / DS) and a DSi .dsi
# (title ID + DSi unit code so WPA2 Wi-Fi works in DSi mode)
#---------------------------------------------------------------------------------
all: $(OUTPUT).nds $(OUTPUT).dsi

$(OUTPUT).nds: $(OUTPUT).elf $(GAME_ICON)
	$(SILENTCMD)ndstool -c $@ -9 $(OUTPUT).elf $(_ARM7_ELF) $(_ADDFILES) \
	-b $(GAME_ICON) "$(GAME_TITLE);$(GAME_SUBTITLE1)"
	@echo built ... $(notdir $@)

$(OUTPUT).dsi: $(OUTPUT).elf $(GAME_ICON)
	$(SILENTCMD)ndstool -c $@ -9 $(OUTPUT).elf $(_ARM7_ELF) $(_ADDFILES) \
	-b $(GAME_ICON) "$(GAME_TITLE);$(GAME_SUBTITLE1)" \
	-g $(GAME_CODE) 00 "$(GAME_TITLE_SHORT)" -z 80040000 -u 00030004
	@echo built ... $(notdir $@)

$(OUTPUT).elf: $(OFILES)

# source files depend on generated headers
$(OFILES_SOURCES) : $(HFILES)

#---------------------------------------------------------------------------------
%.bin.o %_bin.h : %.bin
#---------------------------------------------------------------------------------
	@echo $(notdir $<)
	@$(bin2o)

#---------------------------------------------------------------------------------
%.bmp.o %_bmp.h : %.bmp
#---------------------------------------------------------------------------------
	@echo $(notdir $<)
	@$(bin2o)

#---------------------------------------------------------------------------------
# PNG -> GRF via grit. Conversion flags live in the matching gfx/<name>.grit.
# grit ignores PNG alpha, so first flatten each source to a magenta-keyed RGB
# copy (AA composited over the white page) that grit can key transparency on.
#---------------------------------------------------------------------------------
%.grf : %.png %.grit $(CURDIR)/../tools/flatten_png.py
#---------------------------------------------------------------------------------
	@echo grit $(notdir $<)
	@python3 $(CURDIR)/../tools/flatten_png.py $< $*.flat.png
	$(SILENTCMD)grit $*.flat.png `cat $(filter %.grit,$^)` -ftr -fh! -o$*

#---------------------------------------------------------------------------------
# GRF -> object + header via bin2o
#---------------------------------------------------------------------------------
%.grf.o %_grf.h : %.grf
#---------------------------------------------------------------------------------
	@echo $(notdir $<)
	@$(bin2o)

#---------------------------------------------------------------------------------
# Quantize icon.bmp to a 16-color 4bpp BMP for the ROM banner (ndstool -b).
#---------------------------------------------------------------------------------
$(GAME_ICON): $(GAME_ICON_SRC) $(CURDIR)/../tools/bmp2banner.py
#---------------------------------------------------------------------------------
	@echo banner icon $(notdir $<)
	@python3 $(CURDIR)/../tools/bmp2banner.py $< $@

-include $(DEPSDIR)/*.d

#---------------------------------------------------------------------------------------
endif
#---------------------------------------------------------------------------------------
