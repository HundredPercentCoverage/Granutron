//////////////////////////////////////////////////////////////////////////////////////
// GRANUTRON by Matthew Laughlin (l1429201@qub.ac.uk)								//
//																					//
// Multiple voice delay line granulator												//
//																					//
// Influences - DLGranulator from AudioMulch (Ross Bencina)							//
//			  - GRANULE opcode from Csound (Allan S.C. Lee)							//
//			  - bufsyncgrain~ by Victor Lazzarini (ported to PD by Frank Barknecht) //
//////////////////////////////////////////////////////////////////////////////////////

#include <flext.h>
#include <math.h>

#if !defined(FLEXT_VERSION) || (FLEXT_VERSION < 400)
#error You need at least flext version 0.4.1
#endif

#define PI 3.14159265

class CVoice //Class for the stream variables
{
public:
	int gdur, index;
	int readPtr; //Tap on the delay line
	int gap; //Gap between grains in the voice
	bool voice_on;
	float result;

	float envelope(float);
	CVoice();
};

float CVoice::envelope(float input)
{
	result = 0.0f;
	result = (0.424 - (0.5*cos((2*PI*index)/(gdur-1))) + (0.08*cos((4*PI*index)/(gdur-1)))) * input; //Blackman window
	return result;
	//Maybe an envelope with some sustain would be better?
}

CVoice::CVoice()
{
	gap = 1000; //Set initial values
	gdur = 1000;
	voice_on = false;
	readPtr = 0;
	result = 0.0f;
}

/////////////////////////////////////////////////////////////////////////////////////////////////

class granutron:
public flext_dsp
{
	FLEXT_HEADER(granutron, flext_dsp) //granutron needs to use #defines from flbase.cpp (flext_dsp is parent class)

public:
	granutron(int init); //Constructor
	~granutron(); //Desctructor

protected: //Declare DSP function
	virtual void m_signal(int n, float *const *in, float *const *out);

private:
	float gain; //Gain parameter
	float gdur; //Grain duration (samples)
	int ratio, sr; //Time stretch ratio
	int gap; //Grain gap time (defines density)
	int gapoffs, duroffs; //Random offset for IOI
	int temp;
	float result;
	int numStreams; //Number of streams
	float sig; //Output sample
	CVoice *voice; //Voices for dynamic creation
	float numSecondsDelay;
	int numSamplesDelay;
	float *buffer;
	int writePtr;
	int freeze;

	FLEXT_CALLBACK_F(setGain); //Use these functions as handlers - gain
	void setGain(float f);
	FLEXT_CALLBACK_F(setGap); //Gap between successive grains
	void setGap(float f);
	FLEXT_CALLBACK_F(setGapOff); //Grain gap offset
	void setGapOff(float f);
	FLEXT_CALLBACK_F(setGdur); //Grain duration
	void setGdur(float f);
	FLEXT_CALLBACK_F(setDurOffs); //Grain duration offset
	void setDurOffs(float f);
	FLEXT_CALLBACK_I(setFreeze); //Toggle freezing
	void setFreeze(int s);
	FLEXT_CALLBACK_F(setRatio); //Time stretch ratio handler
	void setRatio(float f);
};

FLEXT_NEW_DSP_1("granutron~ pan~", granutron, int); //Create new DSP class with one arg

granutron::granutron(int init) //Constructor
{
	numStreams = (init>20)?20:(init<1)?1:init; //Set number of voices on creation

	AddInSignal("audio in"); //Create signal inlet called left audio in
	AddInFloat(5); //Create 5 float inlets
	AddInInt("hold writing"); //Select to freeze write pointer on buffer
	AddInFloat("Time stretch"); //Float for time stretch - makes more sense to come after freeze
	AddOutSignal("audio out");

	FLEXT_ADDMETHOD(1, setGain); //Gain function
	FLEXT_ADDMETHOD(2, setGap); //Gap function
	FLEXT_ADDMETHOD(3, setGapOff); //Gap offset function
	FLEXT_ADDMETHOD(4, setGdur); //Grain duration function
	FLEXT_ADDMETHOD(5, setDurOffs); //Grain duration offset function
	FLEXT_ADDMETHOD(6, setFreeze); //Set whether or not the buffer writing is frozen
	FLEXT_ADDMETHOD(7, setRatio);

	voice = new CVoice[numStreams]; //Create voices
	numSecondsDelay = 0.1f; //Should this be variable??
	numSamplesDelay = (int)(numSecondsDelay * Samplerate()); //Set delay length
	buffer = new float[3*numSamplesDelay]; //Set up delay buffer
	writePtr = numSamplesDelay; //Start at middle of buffer
	gapoffs = 1;
	duroffs = 1;
	sig = 0;
	temp = 0;
	freeze = 0;
	ratio = 1;

	for(int i=0;i<numSamplesDelay;i++)
	{
		buffer[i] = 0.0f;
	}

	post("--buffer initialised--");
	post("%i voices", numStreams);
}

granutron::~granutron() //Destructor
{
	delete buffer;
	delete voice;
}

void granutron::m_signal(int n, float *const *in, float *const *out) //DSP function
{
	//"in" holds list of signal vectors in all inlets
	const float *ins1 = in[0]; //Let ins1 hold signal vector of first inlet (index 0)

	//"out" holds list of signal vectors for the outlet
	float *outs = out[0]; //Now contained in outs
	
	while(n--) //Main signal loop
	{
		if(freeze == 0) //If freeze is not selected
		{
			buffer[writePtr] = *ins1++; //Read sample into delay
		}
		sig = 0.0f;

		for(int j=0;j<numStreams;j++)
		{
			if(voice[j].voice_on==true)
			{
				if(++voice[j].index >= voice[j].gdur) //If at end of grain
				{
					post("End of grain");
					sig += voice[j].envelope(buffer[voice[j].readPtr]);
					voice[j].voice_on=false; //Grain is off
					//if(freeze==1)	
					//{
						//voice[j].readPtr = writePtr; //Reset read pointer to start to remove click
					//}
				}
				else
				{
					sig += voice[j].envelope(buffer[voice[j].readPtr]);
					voice[j].readPtr++; //Increment voice delay line tap
				}
			}
			else if(voice[j].voice_on==false)
			{
				if(--voice[j].gap <= 0) //Counter loop - temp is a temporary int holding the grain gap
				{
					if(freeze == 1)
					{
						voice[j].readPtr -= ratio;
						post("%f", voice[j].readPtr);
						if(voice[j].readPtr < 0)
							voice[j].readPtr += (3*numSamplesDelay);
					}
					voice[j].gap = gap+rand()%gapoffs; //Set temp to the onset time in the 5th inlet + offset
					voice[j].voice_on = true; //Activate grain
					voice[j].index = 0; //Set index to start
					voice[j].gdur = gdur+rand()%duroffs; //Grain duration is the current inlet value plus the random offset
				}
			}

			if(voice[j].readPtr >= 3*numSamplesDelay) //Wrap to start of buffer if necessary
				voice[j].readPtr = 0;
		}

		*outs++ = sig*gain; //Output

		if(freeze == 0) //If not frozen
			++writePtr; //Increment buffer writing pointer
		if(writePtr >= 3*numSamplesDelay) //Wrap write pointer if necessary
			writePtr = 0;

	}
}

void granutron::setGain(float f) //These variables must be handled correctly (limits etc)
{
	gain = (f<0.0f)?0.0f:(f>1.0f)?1.0f:f; //Keep within limits
}

void granutron::setGap(float f)
{
	f = (f<1.0f)?1.0f:f; //Things go wrong if gap < 1
	gap = (int)((f/1000)*Samplerate()); //Convert milliseconds to samples
}

void granutron::setGapOff(float f)
{
	f = (f<1.0f)?1.0f:f; //Ramdomising 0 causes problems
	gapoffs = (int)((f/1000)*Samplerate()); //Milliseconds to samples
}

void granutron::setGdur(float f)
{
	f = (f<1.0f)?1.0f:f; //Things go wrong if gdur < 1
	gdur = (int)((f/1000)*Samplerate()); //Convert milliseconds to samples
}

void granutron::setDurOffs(float f)
{
	f = (f<1.0f)?1.0f:f;
	duroffs = (int)((f/1000)*Samplerate()); //Milliseconds to samples
}

void granutron::setFreeze(int s)
{
	freeze = (s>1)?1:(s<0)?0:s; //<=1 means on, 0 means off (can therefore use toggle)
}

void granutron::setRatio(float f)
{
	f = (f>1.0f)?1.0f:(f<0.0f)?0.01f:f; //Keep within limits
	sr = Samplerate();
	ratio = (int)(sr-(sr*f)); //Calculate stretch
}

//IMPORTANT - ERROR HANDLING MUST BE ADDED IE. DISPLAY A MESSAGE FOR UNKNOWN/OUT OF RANGE MESSAGES
//TIME STRETCH