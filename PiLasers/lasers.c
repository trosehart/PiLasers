/*****************************************************************************

File: lasers.c
Purpose: This program records entries and exits from a room using two lasers 
	and two photodiodes.  Depending on which laser is broken first, it can 
	check which direction they went.
Date: November 13, 2018

******************************************************************************/

#include "gpiolib_addr.h"
#include <time.h>

//HARDWARE DEPENDENT CODE BELOW
#include "gpiolib_reg.h"

#include <unistd.h> 
#include <stdlib.h> 
#include <stdio.h>

//for state machine counting entries and exits
enum EntryState {ENTRY_START, BROKE_FIRST, BROKE_SECOND, CLEARED_FIRST, WRONG_WAY, EMPTY, ENTRY_DONE};
//for state machine counting how many times each was broken
enum State {START, DONE, GOT_KTH_ZERO, GOT_KTH_ONE, GOT_ZERO, GOT_ONE};

#ifndef MARMOSET_TESTING

/* You may want to create helper functions for the Hardware Dependent functions*/

//This function should initialize the GPIO pins
GPIO_Handle initializeGPIO() {
    GPIO_Handle gpio;
    gpio = gpiolib_init_gpio();
    if (gpio == NULL) {
        perror("Could not initialize GPIO");
    }
    return gpio;
}

//This function should accept the diode number (1 or 2) and output
//a 0 if the laser beam is not reaching the diode, a 1 if the laser
//beam is reaching the diode or -1 if an error occurs.
#define LASER1_PIN_NUM 17
#define LASER2_PIN_NUM 4
int laserDiodeStatus(GPIO_Handle gpio, int diodeNumber) {

    if (gpio == NULL)
        return -1;

    if (diodeNumber == 1) {
        uint32_t level_reg = gpiolib_read_reg(gpio, GPLEV(0));

        if (level_reg & (1 << LASER1_PIN_NUM))
            return 1;
        else
            return 0;

    }
    else if (diodeNumber == 2) {
        uint32_t level_reg = gpiolib_read_reg(gpio, GPLEV(0));

        if (level_reg & (1 << LASER2_PIN_NUM))            
			return 1;
        else
            return 0;
    } 
    else
        return -1;

}

#endif
//END OF HARDWARE DEPENDENT CODE

//This function will output the number of times each laser was broken
//and it will output how many objects have moved into and out of the room.

//laser1Count will be how many times laser 1 is broken (the left laser).
//laser2Count will be how many times laser 2 is broken (the right laser).
//numberIn will be the number  of objects that moved into the room.
//numberOut will be the number of objects that moved out of the room.
void outputMessage(int laser1Count, int laser2Count, int numberIn, int numberOut) {
    printf("Laser 1 was broken %d times \n", laser1Count);
    printf("Laser 2 was broken %d times \n", laser2Count);
    printf("%d objects entered the room \n", numberIn);
    printf("%d objects exited the room \n", numberOut);
}

//This function accepts an errorCode. You can define what the corresponding error code
//will be for each type of error that may occur.
void errorMessage(int errorCode) {
    fprintf(stderr, "An error occured; the error code was %d \n", errorCode);
}

//this function record the number bits of each photodiode 100 times/s
void recordLasers(GPIO_Handle gpio, int timeLimit, int *laser1, int *laser2) {
	//goes through 100 times per second
    for (int i = 0; i < timeLimit*100; i++) {
		//sets each array point based on if the laser is broken or not
        laser1[i] = laserDiodeStatus(gpio, 1);
        laser2[i] = laserDiodeStatus(gpio, 2);
        usleep(10000);
    }
	return;
}

//function for checking number of entries
//can be used for exits by swapping order of arrays sent
int numEntries(int laser1[], int laser2[], int size) {
    if (size < 0) {
        return -1;
    }

    int entryCount = 0;
    int count = 0;
    
    enum EntryState entries;
    entries = ENTRY_START;

    while (entries != ENTRY_DONE) {
        if ((laser1[count] != 0 && laser1[count] != 1) || (laser2[count] != 0 && laser2[count] != 1)) {
            return -1;
        }

        switch (entries) {
            case ENTRY_START:
                entries = EMPTY;
                break;

            case BROKE_FIRST:
				//checks if second laser is broken
                if (!laser2[count] && !laser1[count]) {
                    entries = BROKE_SECOND;
                }
				//checks if person backed up after breaking first laser
                else if (laser1[count]) {
                    entries = EMPTY;
                }
                break;
			
			case BROKE_SECOND:
				//checks if user cleared first laser
				if(laser1[count] && !laser2[count]) {
					entries = CLEARED_FIRST;
				}
				//if user didn't clear laser 1, but backed up from laser 2
				else if(!laser1[count] && laser2[count]) {
					entries = BROKE_FIRST;
				}
				break;

			case CLEARED_FIRST:
				//checks if they have cleared laser 1 and 2
				if(laser2[count] && laser1[count]) {
					++entryCount;
					entries = EMPTY;
				}
				//checks if they backed up from second laser after clearing the first
				else if(!laser1[count] && !laser2[count]) {
					entries = BROKE_SECOND;
				}
				break;

			case WRONG_WAY:
				//checks if both have been cleared to reset
				if(laser1[count] && laser2[count]) {
					entries = EMPTY;
				}
				break;

            case EMPTY:
				//checks if they have broken the first laser
                if (!laser1[count] && laser2[count]) {
                    entries = BROKE_FIRST;
                }
				//for case when it breaks 1, breaks 2, clears 1, breaks 1, clears 2, clears 1
				if(laser1[count] && !laser2[count]) {
					entries = WRONG_WAY;
				}
                break;

            case ENTRY_DONE:
                break;

            default:
                break;
        }

        ++count;
        if (count >= size) {
            entries = ENTRY_DONE;
        }
    }

    return entryCount;
}

//counting number of times it has been broken, checks for hysteresis
int countZeros(const int zeroOneData[], const int numSamples) {

	enum State s = START;
	const int kMax = 10;
	int zeroCount = 0;
	int oneCount = 0;
	int k1 = 0;
	int k0 = 0;

	if (kMax < 0)
		return -1;
	if (numSamples <= 0)
		return -1;

	for (int i = 0; i < numSamples; i++) {

		if (zeroOneData[i] != 1 && zeroOneData[i] != 0)
			return -1;

		switch (s) {
			case START:
				if (zeroOneData[0] == 0) {
					k0++;
					if (k0 > kMax) {
						zeroCount++;
						s = GOT_ZERO;
                   }
                   else
						s = GOT_KTH_ZERO;
				}
                else if (zeroOneData[0] == 1) {
					k1++;
                    s = GOT_ONE;
                }
                break;
 			case GOT_KTH_ZERO:
				if (zeroOneData[i] == 0) {
					k0++;
                    if (k0 > kMax) {
                    	zeroCount++;
                    	s = GOT_ZERO;
                    }
                    else
                        s = GOT_KTH_ZERO;
				}
                if (zeroOneData[i] == 1) {
                	k0 = 1;
                    if (k1 > kMax) {
                    	s = GOT_ONE;
                    }
                    else
                        s = GOT_ONE;
                }
                break;
			case GOT_KTH_ONE:
                if (zeroOneData[i] == 1) {
                    k1++;
                    if (k1 > kMax) {
                    	oneCount++;
                        s = GOT_ONE;
                    }
                    else
                       s = GOT_KTH_ONE;
				}
                if (zeroOneData[i] == 0) {
                    k1 = 1;
                    if (k0 > kMax) {
                        s = GOT_ZERO;
                    }
                    else  
                        s = GOT_KTH_ZERO;
                }
                break;
            case GOT_ZERO:
                if (zeroOneData[i] == 0) {
                    s = GOT_ZERO;
                }
                if (zeroOneData[i] == 1) {
                    if (kMax >= 1) {
                        s = GOT_KTH_ONE;
                        k1 = 1;
                    }
                    else {
                        oneCount++;
                        s = GOT_ONE;
                    }
                }
                break;
            case GOT_ONE:
                if (zeroOneData[i] == 1) {
                    s = GOT_ONE;
                }
                if (zeroOneData[i] == 0) {
                    if (kMax >= 1) {
                        s = GOT_KTH_ZERO;
                        k0 = 1;
                    }
                    else {
                        zeroCount++;
                        s = GOT_ZERO;
                    }
                }
                break;
            case DONE:
                if (zeroOneData[i-1] == 1 && zeroOneData[i-2] != 1) {
                    if (kMax == 0)
                        oneCount++;
                }
                if (zeroOneData[i-1] == 0 && zeroOneData[i-2] != 0) {
                    if (kMax == 0)
                        zeroCount++;
                }
                if (zeroOneData[i] == 1) {
                    k1++;
                    if (k1 > kMax && zeroOneData[i-1] != 1) {
                        oneCount++;
                    }
                }
                if (zeroOneData[i] == 0) {
                    k0++;
                    if (k0 > kMax && zeroOneData[i-1] != 0) {
                        zeroCount++;
                    }
                }
                return zeroCount;
        }
    }

    return zeroCount;

}

int main(const int argc, const char* const argv[]) {
    if (argc < 2) {
        printf("Error, no time given: exiting\n");
        return -1;
    }
    int timeLimit = atoi(argv[1]);

    GPIO_Handle gpio = initializeGPIO();
	
	int size = timeLimit*100;
	int laser1[size];
	int laser2[size];

	for(int i = 0; i < size; i++) {
		laser1[i] = 0;
		laser2[i] = 0;
	}

	recordLasers(gpio, timeLimit, laser1, laser2);

	int laser1Count = countZeros(laser1, size);
    int laser2Count = countZeros(laser2, size);
    //exits are just reverse entries, so calls function with reversed arrays
    int numIn = numEntries(laser1, laser2, size);
    int numOut = numEntries(laser2, laser1, size);

    //output function
    outputMessage(laser1Count, laser2Count, numIn, numOut);

    //Free the GPIO now that the program is over.
    gpiolib_free_gpio(gpio);
    return 0;
}