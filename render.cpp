#define BELA_DISABLE_CPU_TIME
#define BELA_DONT_INCLUDE_UTILITIES
#define NUM_CHANNELS 2

#include <Bela.h>
#include <libraries/AudioFile/AudioFile.h>
#include <vector>
#include "qsdk/RTProtocol.h"
#include "qsdk/RTPacket.h"

#define BUFFER_LEN 22050   // BUFFER LENGTH

// std::string gFilename = "res/sunnyside.flac";
// std::string gFilename = "res/naughty.flac";
std::string gFilename = "res/gigolo.flac";

// QTM configuration
// marker for when the process of audio playback starts
const char* gPlaybackStartMarker = "playback_start";
// marker for when the process of audio playback ends
const char* gPlaybackEndMarker = "playback_end";
// marker for when the song starts
const char* gSongStartMarker = "song_start";
// the sample index at which the song starts
const unsigned int gSongStartSample = 88200; // specify real value here, 0 is the very first sample in the file
// marker for when the song ends
const char* gSongEndMarker = "song_end";
// the sample index for the end of the song
const unsigned int gSongEndSample = 5292000; // specify real value here, placeholder is 44,100 * 120 (2 minutes)

// the marker to send at each tick interval
const char* gTickMarker = "tick";
// how often in milliseconds to send the tick marker
const unsigned int gTickInterval = 1000; // in ms

// the song sample rate, this should not need to be changed
const unsigned int gBaseSampleRate = 44100;

// how often to print out tick markers 1 = every tick, 0 = disable completely
const unsigned int gPrintEveryNTicks = 10;

// QTM variables

// QTM protocol
CRTProtocol* rtProtocol = NULL;

// QTM communication packet
CRTPacket* rtPacket = NULL;

// QTM packet type (we want Data Packets (CRTPacket::PacketData).
CRTPacket::EPacketType packetType;

// Connect to host (adjust as neccessary)
const char serverAddr[] = "192.168.6.1";
// Default port for QTM is 22222
const unsigned short basePort = 22222;
// Protocol version, 1.23 is the latest
const int majorVersion = 1;
const int minorVersion = 23;
// Leave as false
const bool bigEndian = false;
// server port
unsigned short nPort = 0;

// are we connected to QTM?
bool gConnected = false;


// audio playback variables

int gNumFramesInFile;

// Two buffers for each channel:
// one of them loads the next chunk of audio while the other one is used for playback
std::vector<std::vector<float> > gSampleBuf[2];

// read pointer relative current buffer (range 0-BUFFER_LEN)
// initialise at BUFFER_LEN to pre-load second buffer (see render())
int gReadPtr = BUFFER_LEN;
// the global playback index
unsigned int gPlaybackIndex = 0;
// read pointer relative to file, increments by BUFFER_LEN (see fillBuffer())
int gBufferReadPtr = 0;
// keeps track of which buffer is currently active (switches between 0 and 1)
int gActiveBuffer = 0;
// this variable will let us know if the buffer doesn't manage to load in time
int gDoneLoadingBuffer = 1;
// if file read is complete this variable is set to true
bool gDoneReadingFile = false;
// just to indicate that after the next buffer read we should finish up
bool gTerminateAfterRead = false;
// this gets set to true to stop playback and end the program
bool gStop = false;

// convert the ms time to a number of samples
const unsigned int gTickIntervalSamples = gTickInterval * gBaseSampleRate / 1000;

// keep track of the current tick for printing
unsigned int gTickCounter = 0;
// keep track of tick printing
unsigned int gTickPrint = 0;

// The possible label types
enum Event {
    kPlaybackStart = 0,
    kPlaybackEnd,
    kSongStart,
    kSongEnd
};

// The current label type
// Event gRequestedEvent;

enum EventStatus {
    kEventPending = 0,
    kEventRequested,
    kEventConfirmed,
    kEventFailed
};

bool gEventRequested = false;

// keep track of which labels have already been sent
std::vector<EventStatus> gEventStatus = {
    EventStatus::kEventPending, // kPlaybackStart
    EventStatus::kEventPending, // kPlaybackEnd
    EventStatus::kEventPending, // kSongStart
    EventStatus::kEventPending // kSongEnd
};

// the event labels, in a vector
const std::vector<const char*> gEventLabels = {
    gPlaybackStartMarker,
    gPlaybackEndMarker,
    gSongStartMarker,
    gSongEndMarker
};

AuxiliaryTask gFillBufferTask;

AuxiliaryTask gTickLabelMarkerTask;

AuxiliaryTask gEventLabelMarkerTask;

void fillBuffer(void*) {

    // increment buffer read pointer by buffer length
    gBufferReadPtr+=BUFFER_LEN;

    // reset buffer pointer if it exceeds the number of frames in the file
    if(gBufferReadPtr>=gNumFramesInFile)
        gBufferReadPtr=0;

    int endFrame = gBufferReadPtr + BUFFER_LEN;
    int zeroPad = 0;

    // if reaching the end of the file take note of the last frame index
    // so we can zero-pad the rest later
    if((gBufferReadPtr+BUFFER_LEN)>=gNumFramesInFile-1) {
          endFrame = gNumFramesInFile-1;
          zeroPad = 1;
    }

    for(unsigned int ch = 0; ch < gSampleBuf[0].size(); ++ch) {

        // fill (nonactive) buffer
        AudioFileUtilities::getSamples(gFilename,gSampleBuf[!gActiveBuffer][ch].data(),ch
                    ,gBufferReadPtr,endFrame);

        // zero-pad if necessary
        if(zeroPad) {
            int numFramesToPad = BUFFER_LEN - (endFrame-gBufferReadPtr);
            for(int n=0;n<numFramesToPad;n++)
                gSampleBuf[!gActiveBuffer][ch][n+(BUFFER_LEN-numFramesToPad)] = 0;
            
            gDoneReadingFile = true;
        }

    }

    gDoneLoadingBuffer = 1;

}


// This function sends a label to QTM to annotate the capture
bool sendQTMLabel(const char* label, const bool verbose = false) {
	if (verbose) printf("\nSending label %s...\n", label);
	
    const bool result = rtProtocol->SetQTMEvent(label);
    if (!result) {
        const char* errorStr = rtProtocol->GetErrorString();
        printf("\nError sending event label (%s): %s\n", label, errorStr);
    }
    // send the result back, true if successful, false if not
    return result;
}

// this is the bela scheduled task wrapper for QTM tick labels
void tickLabelMarker(void*) {
    // if we are not finishing up, send a tick marker
    if (!gStop) {
        sendQTMLabel(gTickMarker);
        if(gPrintEveryNTicks > 0) {
        	gTickPrint += 1;
        	printf(".");
        	if (gTickPrint%gPrintEveryNTicks == 0) {
        		gTickPrint = 0;
        		fflush(stdout);
        	}
        }
    }
}

// bela scheduled task for QTM event labels
void eventLabelMarker(void*) {
    // if we use output buffering, flush any previous messages before sending the label
    if(gPrintEveryNTicks > 0) {
        fflush(stdout);
    }
    // loop through all event label statuses
    for (int i = 0; i < gEventStatus.size(); i++) {
        // if an event label has been requested, send it
        if (gEventStatus[i] == EventStatus::kEventRequested) {
            // send the label to QTM
            if(sendQTMLabel(gEventLabels[i])) {
                // if successful, set the status to confirmed
                gEventStatus[i] = EventStatus::kEventConfirmed;
            } else {
                // if not successful, set the status to failed
                gEventStatus[i] = EventStatus::kEventFailed;
            }
        }
    }

}

bool setup(BelaContext *context, void *userData)
{

    // Initialise auxiliary tasks
	if((gFillBufferTask = Bela_createAuxiliaryTask(&fillBuffer, 90, "fill-buffer")) == 0)
		return false;

    // initialise tick label marker task
    if ((gTickLabelMarkerTask =
           Bela_createAuxiliaryTask(&tickLabelMarker, 60, "mark-labels")) == 0)
        return false;
    // initialise event label marker task
    if ((gEventLabelMarkerTask =
           Bela_createAuxiliaryTask(&eventLabelMarker, 50, "mark-labels")) == 0)
        return false;

    gNumFramesInFile = AudioFileUtilities::getNumFrames(gFilename);

    if(gNumFramesInFile <= 0)
        return false;

    if(gNumFramesInFile <= BUFFER_LEN) {
        printf("Sample needs to be longer than buffer size. This example is intended to work with long samples.");
        return false;
    }

    // load the file up to the specified buffer length
	gSampleBuf[0] = AudioFileUtilities::load(gFilename, BUFFER_LEN, 0);
	gSampleBuf[1] = gSampleBuf[0]; // initialise the inactive buffer with the same channels and frames as the first one

    // initialise the QTM realtime protocol
    rtProtocol = new CRTProtocol();
    // Connect to the QTM application
    if (!rtProtocol->Connect(serverAddr, basePort, &nPort, majorVersion, minorVersion,
                            bigEndian)) {
        printf("\nFailed to connect to QTM RT Server. %s\n\n", rtProtocol->GetErrorString());
                    system("pause");
        return false;
    }
    gConnected = true;
    printf("Connected to QTM...");

    // allocate char to stor version
    char qtmVer[64];
    // request version from QTM
    rtProtocol->GetQTMVersion(qtmVer, sizeof(qtmVer));
    // print the version
    printf("%s\n", qtmVer);

    // taking control of QTM
    if (!rtProtocol->TakeControl()) {
        printf("Failed to take control of QTM RT Server. %s\n",
            rtProtocol->GetErrorString());
        return false;
    }
    printf("Took control of QTM.\n"); 

	return true;
}

void render(BelaContext *context, void *userData)
{
    // have we reached the end of playback?
    if (gStop) {
        // has the end marker been sent?
        if (gEventStatus[Event::kPlaybackEnd] != EventStatus::kEventConfirmed) {
            // have we requested QTM to send the label?
            if (gEventStatus[Event::kPlaybackEnd] != EventStatus::kEventRequested) {
                // mark the event as requested
                gEventStatus[Event::kPlaybackEnd] = EventStatus::kEventRequested;
                // schedule the label task
                // we cannot call bela stop from here, it needs to be done
                // in the auxillary task in case the program stops before the task completes.
                Bela_scheduleAuxiliaryTask(gEventLabelMarkerTask);
            }
            
        }
        
        // we will just write silence to the audio output until the rest finishes up.
        for (unsigned int n = 0; n < context->audioFrames; n++) {
            for (unsigned int ch = 0; ch < context->audioOutChannels; ch++) {
                audioWrite(context, n, ch, 0);
            }
        }
        return;

    }
    for(unsigned int n = 0; n < context->audioFrames; n++) {
        gEventRequested = false;

        // Increment read pointer and reset to 0 when end of file is reached
        if(++gReadPtr >= BUFFER_LEN) {
            // if the file hasn't finished reading, request the next buffer to be filled
            if (!gDoneReadingFile) {
                if(!gDoneLoadingBuffer)
                    rt_printf("\nCouldn't load buffer in time :( -- try increasing buffer size!\n");
                gDoneLoadingBuffer = 0;
                gReadPtr = 0;
                gActiveBuffer = !gActiveBuffer;
                
                // request the inactive buffer to be filled
                Bela_scheduleAuxiliaryTask(gFillBufferTask);
                
            } else {
                // if we're done reading, and we're at the final buffer, request program stop
                if(gTerminateAfterRead) {
                	if (!gStop) {
                    	rt_printf("\nDone reading file, exiting after buffer playback...\n");
                    	gStop = true;
                	}
                } else {
                    // otherwise we have to read the final buffer
                    gReadPtr = 0;
                    gActiveBuffer = !gActiveBuffer;
                    gTerminateAfterRead = true;
                }
            }
            
        }

        // write the buffer to the audio output
    	for(unsigned int channel = 0; channel < NUM_CHANNELS; channel++) {
            float out = gSampleBuf[gActiveBuffer][channel][gReadPtr];
            audioWrite(context, n, channel, out);
    	}

        // Send event labels if appropriate
        // are we at the start of the file?
        if (gEventStatus[Event::kPlaybackStart] != EventStatus::kEventConfirmed && gPlaybackIndex >= 0) {
            // if we haven't already requested the label, send it now
            if (gEventStatus[Event::kPlaybackStart] != EventStatus::kEventRequested) {
                // mark the event as requested
                gEventStatus[Event::kPlaybackStart] = EventStatus::kEventRequested;
                gEventRequested = true;
            }
        }

        // are we at the start of the song?
        if (gEventStatus[Event::kSongStart] != EventStatus::kEventConfirmed && gPlaybackIndex >= gSongStartSample) {
            // if we haven't already requested the label, send it now
            if (gEventStatus[Event::kSongStart] != EventStatus::kEventRequested) {
                // mark the event as requested
                gEventStatus[Event::kSongStart] = EventStatus::kEventRequested;
                gEventRequested = true;
            }
        }

        // are we at the end of the song?
        // the order is of comparison is switched for fast failing (more likely to be false for longer)
        if (gPlaybackIndex >= gSongEndSample && gEventStatus[Event::kSongEnd] != EventStatus::kEventConfirmed) {
            // if we haven't already requested the label, send it now
            if (gEventStatus[Event::kSongEnd] != EventStatus::kEventRequested) {
                // mark the event as requested
                gEventStatus[Event::kSongEnd] = EventStatus::kEventRequested;
                gEventRequested = true;
            }
        }

        if (gEventRequested) {
            // if we have requested an event, schedule the task to send the event label to QTM
            Bela_scheduleAuxiliaryTask(gEventLabelMarkerTask);
        }

        // keep track of how many frames we've played (relative to last tick label)
        gTickCounter++;
        // the global sample index
        gPlaybackIndex++;
        // if we've played enough frames, send a label to QTM
        if (gTickCounter >= gTickIntervalSamples) {
            // schedule the tick label task, if the result is 0, it was successful
            if(Bela_scheduleAuxiliaryTask(gTickLabelMarkerTask) == 0) {
                // reset the counter
                gTickCounter = 0;
            }
        }
    }
}


void cleanup(BelaContext *context, void *userData)
{
    // if QTM was connected, disconnect nicely on exit
    if (gConnected) {
        gConnected = false;
        rtProtocol->Disconnect();
    }
}
