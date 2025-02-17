#!/bin/sh
set -e

# This file is part of the CMaNGOS Project. See AUTHORS file for Copyright information
#
# This file is free software; as a special exception the author gives
# unlimited permission to copy and/or distribute it, with or without
# modifications, as long as this notice is preserved.
#
# This program is distributed in the hope that it will be useful, but
# WITHOUT ANY WARRANTY, to the extent permitted by law; without even the
# implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

## Expected param 1 to be 'a' for all, else ask some questions
## optionally param 1 or param 2 is the path to game client

PREFIX="$(dirname $0)"

## Normal log file (if not overwritten by second param)
LOG_FILE="MaNGOSExtractor.log"
## Detailed log file
DETAIL_LOG_FILE="MaNGOSExtractor_detailed.log"

## ! Use below only for finetuning or if you know what you are doing !

USE_AD="0"
USE_VMAPS="0"
USE_MMAPS="0"
USE_MMAPS_OFFMESH="0"
USE_MMAPS_DELAY=""
AD_RES=""
VMAP_RES=""
NUM_THREAD=""

if [ "$1" != "a" ] && [ "x$1" != "x" ]
then
  CLIENT_PATH="$1"
  OUTPUT_PATH="$2"
elif [ "x$2" != "x" ]
then
  CLIENT_PATH="$2"
  OUTPUT_PATH="$3"
fi

if [ "x${CLIENT_PATH}" != "x" ]
then
  if [ ! -d "${CLIENT_PATH}/Data" ]
  then
    echo "Data folder not found in provided client path [${CLIENT_PATH}]. Plese provide a correct client path."
    exit 1
  fi
else
  if [ ! -d "$(pwd)/Data" ]
  then
    echo "Data folder not found. Make sure you have copied the script to the client folder and the 'Data' folder has the correct case."
    exit 1
  fi
fi

if [ "x${OUTPUT_PATH}" != "x" ]
then
  if [ ! -d "${OUTPUT_PATH}" ]
  then
    echo "Provided OUTPUT_PATH=${OUTPUT_PATH} does not exist, please create it before"
    exit 1
  fi
  LOG_FILE="${OUTPUT_PATH:-.}/${LOG_FILE}"
  DETAIL_LOG_FILE="${OUTPUT_PATH:-.}/${DETAIL_LOG_FILE}"
fi

if [ "$1" = "a" ]
then
  ## extract all
  USE_AD="1"
  USE_VMAPS="1"
  USE_MMAPS="1"
  USE_MMAPS_DELAY="no"
else
  ## do some questioning!
  echo
  echo "Welcome to helper script to extract required dataz for MaNGOS!"
  echo "Should all dataz (dbc, maps, vmaps and mmaps) be extracted? (y/n)"
  read line
  if [ "$line" = "y" ]
  then
    ## extract all
    USE_AD="1"
    USE_VMAPS="1"
    USE_MMAPS="1"
  else
    echo
    echo "Should dbc and maps be extracted? (y/n)"
    read line
    if [ "$line" = "y" ]; then USE_AD="1"; fi

    echo
    echo "Should vmaps be extracted? (y/n)"
    read line
    if [ "$line" = "y" ]; then USE_VMAPS="1"; fi

    echo
    echo "Should mmaps be extracted? (y/n)"
    echo "WARNING! This may take several hours with small number of CPU threads!"
    read line
    if [ "$line" = "y" ]
    then
      USE_MMAPS="1";
    else
      echo
      echo "Only reextract offmesh tiles for mmaps? (y/n)"
      read line
      if [ "$line" = "y" ]
      then
        USE_MMAPS_OFFMESH="1";
      fi
    fi
  fi
fi

if [ "x$CLIENT_PATH" != "x" ]
then
  AD_OPT_RES="-i $CLIENT_PATH"
  VMAP_OPT_RES="-d $CLIENT_PATH/Data"
fi

if [ "x$OUTPUT_PATH" != "x" ] && [ -d "$OUTPUT_PATH" ]
then
  AD_OPT_RES="$AD_OPT_RES -o $OUTPUT_PATH"
  VMAP_OPT_RES="$VMAP_OPT_RES -o $OUTPUT_PATH"
fi

## Special case: Only reextract offmesh tiles
if [ "$USE_MMAPS_OFFMESH" = "1" ]
then
  echo "Only extracting offmesh tiles"
  $PREFIX/MoveMapGen.sh offmesh "$OUTPUT_PATH" $LOG_FILE $DETAIL_LOG_FILE
  exit 0
fi

## MMap Extraction specific
if [ "$USE_MMAPS" = "1" ]
then
  ## Obtain number of processes
  echo "How many CPU threads should be used for extracting mmaps? (leave empty to use all available threads)"
  read line
  echo
  if [[ ! -z $line ]]; then
    if [[ $line =~ ^[1-9+]$ ]]; then
      NUM_THREAD=$line
    else
      echo "Only numbers are allowed!"
      exit 1
    fi
  else
    NUM_THREAD="all"
  fi
  ## Extract MMaps delayed?
  if [ "$USE_MMAPS_DELAY" != "no" ]; then
    echo "MMap extraction can be started delayed"
    echo "If you do _not_ want MMap Extraction to start delayed, just press return"
    echo "Else enter number followed by s for seconds, m for minutes, h for hours"
    echo "Example: \"3h\" - will start mmap extraction in 3 hours"
    echo
    echo "MMap Extraction Delay (leave blank for direct extraction):"
    read USE_MMAPS_DELAY
  else
    USE_MMAPS_DELAY=""
  fi
fi

## Give some status
echo
echo "Current Settings:"
echo "Extract DBCs/maps: $USE_AD, Extract vmaps: $USE_VMAPS, Extract mmaps: $USE_MMAPS, Processes for mmaps: $NUM_THREAD"
if [ "$USE_AD" = "1" ] && [ "$AD_RES" = "-f 0" ]; then
  echo "maps extraction will be high-resolution";
fi
if [ "$USE_VMAPS" = "1" ] && [ "$VMAP_RES" = "-l" ]; then
  echo "vmaps extraction will be high-resolution";
fi
if [ "$USE_MMAPS_DELAY" != "" ]; then
  echo "MMap Extraction will be started delayed by $USE_MMAPS_DELAY"
fi
echo
if [ "$1" != "a" ]
then
  echo "Press (Enter) to continue, or interrupt with (CTRL+C)"
  read line
fi

echo "$(date): Start extracting dataz for MaNGOS" | tee $LOG_FILE

## Handle log messages
if [ "$USE_AD" = "1" ];
then
  echo "DBC and map files will be extracted" | tee -a $LOG_FILE
else
  echo "DBC and map files won't be extracted!" | tee -a $LOG_FILE
fi
if [ "$USE_VMAPS" = "1" ]
then
  echo "Vmaps will be extracted" | tee -a $LOG_FILE
else
  echo "Vmaps won't be extracted!" | tee -a $LOG_FILE
fi
if [ "$USE_MMAPS" = "1" ]
then
  echo "Mmaps will be extracted using $NUM_THREAD CPU threads" | tee -a $LOG_FILE
else
  echo "Mmaps files won't be extracted!" | tee -a $LOG_FILE
fi
echo | tee -a $LOG_FILE

echo "$(date): Start extracting MaNGOS data: DBCs/maps $USE_AD, vmaps $USE_VMAPS, mmaps $USE_MMAPS using $NUM_THREAD CPU threads" | tee $DETAIL_LOG_FILE
echo | tee -a $DETAIL_LOG_FILE

## Extract dbcs and maps
if [ "$USE_AD" = "1" ]
then
 echo "$(date): Start extraction of DBCs and map files..." | tee -a $LOG_FILE
 $PREFIX/ad $AD_RES $AD_OPT_RES | tee -a $DETAIL_LOG_FILE
 echo "$(date): Extracting of DBCs and map files finished" | tee -a $LOG_FILE
 echo | tee -a $LOG_FILE
 echo | tee -a $DETAIL_LOG_FILE
fi

## Extract vmaps
if [ "$USE_VMAPS" = "1" ]
then
  echo "$(date): Start extraction of vmaps..." | tee -a $LOG_FILE
  $PREFIX/vmap_extractor $VMAP_RES $VMAP_OPT_RES | tee -a $DETAIL_LOG_FILE
  exit_code="${PIPESTATUS[0]}"
  if [[ "$exit_code" -ne "0" ]]; then
    echo "$(date): Extraction of vmaps failed with errors. Aborting extraction. See the log file for more details."
    exit "$exit_code"
  fi
  echo "$(date): Extracting of vmaps finished" | tee -a $LOG_FILE
  if [ ! -d "$(pwd)/vmaps" ]
  then
    mkdir ${OUTPUT_PATH:-.}/vmaps
  fi
  echo "$(date): Start assembling of vmaps..." | tee -a $LOG_FILE
  $PREFIX/vmap_assembler ${OUTPUT_PATH:-.}/Buildings ${OUTPUT_PATH:-.}/vmaps | tee -a $DETAIL_LOG_FILE
  exit_code="${PIPESTATUS[0]}"
  if [[ "$exit_code" -ne "0" ]]; then
    echo "$(date): Assembling of vmaps failed with errors. Aborting extraction. See the log file for more details."
    exit "$exit_code"
  fi
  echo "$(date): Assembling of vmaps finished" | tee -a $LOG_FILE

  echo | tee -a $LOG_FILE
  echo | tee -a $DETAIL_LOG_FILE
fi

## Extract mmaps
if [ "$USE_MMAPS" = "1" ]
then
  if [ "$USE_MMAPS_DELAY" != "" ]; then
    echo "Extracting of MMaps is set to be started delayed by $USE_MMAPS_DELAY"
    echo "Current time: $(date)"
    sleep $USE_MMAPS_DELAY
  fi
  sh MoveMapGen.sh "maps" "$OUTPUT_PATH" $LOG_FILE $DETAIL_LOG_FILE $NUM_THREAD
fi
