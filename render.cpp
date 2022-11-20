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
const char* gPlaybackStartMarker = "playback_start";
const char* gPlaybackEndMarker = "playback_end";
const char* gTickMarker = "tick";

const unsigned int gTickInterval = 1000; // in ms

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
// read pointer relative to file, increments by BUFFER_LEN (see fillBuffer())
int gBufferReadPtr = 0;
// keeps track of which buffer is currently active (switches between 0 and 1)
int gActiveBuffer = 0;
// this variable will let us know if the buffer doesn't manage to load in time
int gDoneLoadingBuffer = 1;
// if file read is complete this variable is set to true
bool gDoneReadingFile = false;
bool gTerminateAfterRead = false;
bool gStop = false;

const unsigned int gTickIntervalSamples = gTickInterval * gBaseSampleRate / 1000;
unsigned int gTickCounter = 0;
unsigned int gTickPrint = 0;

AuxiliaryTask gFillBufferTask;

AuxiliaryTask gLabelMarkerTask;

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

    //printf("done loading buffer!\n");

}

void sendQTMLabel(const char* label, const bool verbose = false) {
	if (verbose) printf("\nSending label %s...\n", label);
	
    const bool result = rtProtocol->SetQTMEvent(label);
    if (!result) {
        const char* errorStr = rtProtocol->GetErrorString();
        printf("\nError sending event label (%s): %s\n", label, errorStr);
    }
}

void labelMarker(void*) {
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
        
    } else {
    	if(gPrintEveryNTicks > 0) {
    		fflush(stdout);
    	}
        sendQTMLabel(gPlaybackEndMarker, true);
        Bela_requestStop();
    }
}

bool setup(BelaContext *context, void *userData)
{

    // Initialise auxiliary tasks
	if((gFillBufferTask = Bela_createAuxiliaryTask(&fillBuffer, 90, "fill-buffer")) == 0)
		return false;

    if ((gLabelMarkerTask =
           Bela_createAuxiliaryTask(&labelMarker, 60, "mark-labels")) == 0)
        return false;

    gNumFramesInFile = AudioFileUtilities::getNumFrames(gFilename);

    if(gNumFramesInFile <= 0)
        return false;

    if(gNumFramesInFile <= BUFFER_LEN) {
        printf("Sample needs to be longer than buffer size. This example is intended to work with long samples.");
        return false;
    }

	gSampleBuf[0] = AudioFileUtilities::load(gFilename, BUFFER_LEN, 0);
	gSampleBuf[1] = gSampleBuf[0]; // initialise the inactive buffer with the same channels and frames as the first one

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

    sendQTMLabel(gPlaybackStartMarker, true);

	return true;
}

void render(BelaContext *context, void *userData)
{
    if (gStop) {
        Bela_scheduleAuxiliaryTask(gLabelMarkerTask);
        // we will just write silence to the audio output until the rest finishes up.
        for (unsigned int n = 0; n < context->audioFrames; n++) {
            for (unsigned int ch = 0; ch < context->audioOutChannels; ch++) {
                audioWrite(context, n, ch, 0);
            }
        }
        return;

    }
    for(unsigned int n = 0; n < context->audioFrames; n++) {

        // Increment read pointer and reset to 0 when end of file is reached
        if(++gReadPtr >= BUFFER_LEN) {
            if (!gDoneReadingFile) {
                if(!gDoneLoadingBuffer)
                    rt_printf("\nCouldn't load buffer in time :( -- try increasing buffer size!\n");
                gDoneLoadingBuffer = 0;
                gReadPtr = 0;
                gActiveBuffer = !gActiveBuffer;
                
                Bela_scheduleAuxiliaryTask(gFillBufferTask);
                
            } else {
                if(gTerminateAfterRead) {
                	if (!gStop) {
                    	rt_printf("\nDone reading file, exiting after buffer playback...\n");
                    	gStop = true;
                	}
                } else {
                    gReadPtr = 0;
                    gActiveBuffer = !gActiveBuffer;
                    gTerminateAfterRead = true;
                }
            }
            
        }

    	for(unsigned int channel = 0; channel < NUM_CHANNELS; channel++) {
		float out = gSampleBuf[gActiveBuffer][channel][gReadPtr];
    		audioWrite(context, n, channel, out);
    	}

    }
    gTickCounter += context->audioFrames;
    if (gTickCounter >= gTickIntervalSamples) {
        Bela_scheduleAuxiliaryTask(gLabelMarkerTask);
        gTickCounter = 0;
    }
}


void cleanup(BelaContext *context, void *userData)
{
    if (gConnected) {
        gConnected = false;
        rtProtocol->Disconnect();
    }
}
