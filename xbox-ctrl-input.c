#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/time.h>
//#define DEBUG_MESSAGES
//#ifdef DEBUG_MESSAGES
//	#define DEBUG(x) printf("%s\n", x);
//#else
//	#define DEBUG(x)
//#endif

#define IGNORE_LINE		0
#define PID_SEARCH 		1
#define EVENT_SEARCH 	2

struct timeval start; // reported times are be relative to this start time

//https://www.gnu.org/software/libc/manual/html_node/Calculating-Elapsed-Time.html
int timeval_subtract (struct timeval *result, struct timeval *x, struct timeval *y){
	/* Perform the carry for the later subtraction by updating y. */
	if (x->tv_usec < y->tv_usec) {
		int nsec = (y->tv_usec - x->tv_usec) / 1000000 + 1;
		y->tv_usec -= 1000000 * nsec;
		y->tv_sec += nsec;
	}
	if (x->tv_usec - y->tv_usec > 1000000) {
		int nsec = (x->tv_usec - y->tv_usec) / 1000000;
		y->tv_usec += 1000000 * nsec;
		y->tv_sec -= nsec;
	}

	/* Compute the time remaining to wait.
	tv_usec is certainly positive. */
	result->tv_sec = x->tv_sec - y->tv_sec;
	result->tv_usec = x->tv_usec - y->tv_usec;

	/* Return 1 if result is negative. */
	return x->tv_sec < y->tv_sec;
}


// Print event details to stdout
void printEvent(struct input_event *event){
		struct timeval result;
		timeval_subtract(&result, &event->time, &start);
		puts("----------------------------------------");
		printf(" Time: %li.%li sec\n Type: ", result.tv_sec, result.tv_usec);

		switch (event->type){
			case EV_SYN:
				printf("EV_SYN\n");
				printf(" Code: %i\n", event->code);
				printf("Value: %i\n", event->value);
				break;
			case EV_KEY:
				break;
			case EV_REL:
				printf("EV_REL\n");
				printf(" Code: %i\n", event->code);
				printf("Value: %i\n", event->value);
				break;
			case EV_ABS:
				if (event->code == ABS_X){
					printf("Left Thumbstick X-Value = %i\n", event->value); // 16-bit ADC
				} else if (event->code == ABS_Y){
					printf("Left Thumbstick Y-Value = %i\n", event->value); // 16-bit ADC
				} else if (event->code == 2){
					printf("Left Trigger %i\n", event->value); // 10-bit ADC
				} else if (event->code == ABS_RX){
					printf("Right Thumbstick X-Value = %i\n", event->value); // 16-bit ADC
				} else if (event->code == ABS_RY){
					printf("Right Thumbstick Y-Value = %i\n", event->value); // 16-bit ADC
				} else if (event->code == 5){
					printf("Right Trigger %i\n", event->value); // 10-bit ADC
				} else if (event->code == ABS_HAT0X){
					if (event->value == 1){
						printf("D-pad Right\n");
					} else if (event->value == -1){
						printf("D-pad Left\n");
					} else {
						printf("D-pad Center\n");
					}
				} else if (event->code == ABS_HAT0Y){
					if (event->value == 1){
						printf("D-pad Down\n");
					} else if (event->value == -1){
						printf("D-pad Up\n");
					} else {
						printf("D-pad Center\n");
					}
				} else {
					printf("EV_ABS\n");
					printf(" Code: %i\n", event->code);
					printf("Value: %i\n", event->value);
				}
				break;
			default:
				printf("other\n");
				printf(" Code: %i\n", event->code);
				printf("Value: %i\n", event->value);
		}
		
		if (event->type == EV_KEY){
			switch (event->code){
				case BTN_NORTH:
					printf("X ");
					break;
				case BTN_SOUTH:
					printf("A ");
					break;
				case BTN_EAST:
					printf("B ");
					break;
				case BTN_WEST:
					printf("Y ");
					break;
				case BTN_MODE:
					printf("Home ");
					break;
				case BTN_START:
					printf("Start ");
					break;
				case BTN_SELECT:
					printf("Select ");
					break;
				case BTN_TL:
					printf("Left bumper ");
					break;
				case BTN_TR:
					printf("Right bumber ");
					break;
				case BTN_THUMBL:
					printf("Left Thumbstick Down ");
					break;
				case BTN_THUMBR:
					printf("Right Thumbstick Down ");
					break;
				default:
					printf("Other key. Code = %i ", event->code);
			}

			if (event->value){
				printf("pressed\n");
			} else {
				printf("released\n");
			}
		}
}

/***************************************************************************************************************
 * This function searches /proc/bus/input/devices for a Microsoft Corp. Xbox One S Controller (product=02ea)
 * If the Xbox controller is found, the number (XX) of the udev event to read from /dev/input/eventXX is returned.
 * If an Xbox controller is not found, then -1 is returned.
 **************************************************************************************************************/
int getUdevEventNumber(){
	const char *PID_SEARCH_TERM 	= "Product=02ea"; // identifier for xbox controller
	const char *EVENT_SEARCH_TERM 	= "event"; 	// udev event handler will be followed by number like 'event18'
	const ssize_t BUFFER_SIZE 		= 4096;		// size of data buffer
	char buf[BUFFER_SIZE];	// buffer to hold data read from /proc/bus/input/devices
	ssize_t bytesRead;		// return value of read() function indicating how many bytes were read
	int devfd;				// file descriptor of /proc/bus/input/devices
	int searchStatus 	= IGNORE_LINE; // indicates what search term to compare buffer characters to
	int searchIdx 		=  0; // index of current search term character to compare
	int pidMatched 		=  0; // non-zero when pid matched to PID_SEARCH_TERM
	int newline 		=  1; // non-zero when a newline character is read
	int eventNumber		= -1; // udev event number of matched controller or -1 if not found

	// read input device info from /proc/bus/input/devices
	if ((devfd = open("/proc/bus/input/devices", O_RDONLY)) == -1){ 
		puts("Cannot read /proc/bus/input/devices.");
		return -1;
	}

	while ((bytesRead = read(devfd, buf, BUFFER_SIZE)) > 0) {
		int idx	= 0; // index of current chararcter to analyze from buffer (buf)

		// analyze current block just read from /proc/bus/input/devices
		while (idx < bytesRead){
			char c = buf[idx]; // character at current index (idx) in buffer (buf)

			// Determine if on a line containing product id or handler info to be searched
			if (newline){
				if (c == 'I'){ // search for appropriate product id
					searchStatus = PID_SEARCH;
				} else if (pidMatched && c == 'H'){ // find udev event handler
					searchStatus = EVENT_SEARCH;
				} else { // don't need to look at this line
					searchStatus = IGNORE_LINE;
				}
			}

			// detect product id or handler search terms if necessary
			if (searchStatus == PID_SEARCH){
				if (searchIdx < 12 && c == PID_SEARCH_TERM[searchIdx]){
					searchIdx++;
				} else {
					// determine if pid has been matched (searchIdx >= 12)
					// keep searching if pid hasn't been matched
					searchStatus = PID_SEARCH * !(pidMatched = searchIdx >= 12);
					searchIdx = 0;
				}
			} else if (searchStatus == EVENT_SEARCH){
				if (searchIdx < 5 && c == EVENT_SEARCH_TERM[searchIdx]){
					searchIdx++;
				} else if (searchIdx >= 5){
					// found udev eventXX pattern. extract eventNumber (XX)
					eventNumber = 0;
					while (buf[idx] >= '0' && buf[idx] <= '9'){
						eventNumber = eventNumber * 10 + buf[idx] - '0';
						idx++;
					}
					close(devfd); // close /proc/bus/input/devices
					return eventNumber;

				} else {
					searchIdx = 0; // no match. reset to beginning of search term
				}
			}

			newline = (c == '\n'); // indicate if newline detected
			idx++; 
		}
		
	}

	close(devfd);
	return -1;
}

int main(int argc, char *argv[]){
	char dev[20];// device 'file' location 
	int evNum; 	// udev event number
	int n = 50;	// number of input events to show (default is 50)
	int fd;		// file descriptor of input device
	struct input_event ev; // store information of the current input event

	//printf("Controller udev event%i\n", getUdevEventNumber());

	// determine if xbox controller is attached and what udev event to use
	if ((evNum = getUdevEventNumber()) < 0){
		puts("An Xbox controller (product id = 02ea) was not found.");
		return -1;
	}

	// use udev event number to get device in /dev
	sprintf(dev, "/dev/input/event%d", evNum);
	printf("Xbox controller found. Opening %s\n", dev);

	// set number of input events to show if user provides this as argument
	if (argc == 2){
		char *ptr;
		n = strtol(argv[1], &ptr, 10);
	}

	// Open device in read-only mode. Return if device could not be opened.
	if ((fd = open(dev, O_RDONLY)) == -1){ 
		puts("Unable to open device");
		return errno;
	}
	printf("Opened %s to read and display the next %i input events.\n", dev, n);
	
	// Get start time in seconds
	gettimeofday(&start, NULL);

	// Print n number of inputs
	while (n-- > 0){
		ssize_t size = read(fd, &ev, sizeof(ev));
		if (ev.type != EV_SYN){
			printEvent(&ev);
		}
	}

	close(fd);
	return 0;
}

/*
	// Print values of MACROS for troubleshooting
	printf("BTN_THUMBL = %i\nBTN_THUMBR = %i\n", BTN_THUMBL, BTN_THUMBR);
	printf("Left Thumbstick X = %i\nLeft Thumbstick Y = %i\n", ABS_X, ABS_Y);
	printf("Right Thumbstick X = %i\nRight Thumbstick Y = %i\n", ABS_RX, ABS_RY);

	printf("BTN_DPAD_UP = %i\nBTN_DPAD_DOWN = %i\n", BTN_DPAD_UP, BTN_DPAD_DOWN);
	printf("BTN_DPAD_LEFT = %i\nBTN_DPAD_RIGHT = %i\n", BTN_DPAD_LEFT, BTN_DPAD_RIGHT);

	printf("BTN_TR = %i\nBTN_TL = %i\n", BTN_TR, BTN_TL);
	printf("BTN_TR2 = %i\nBTN_TL2 = %i\n", BTN_TR2, BTN_TL2);

	printf("ABS_HAT0X = %i\nABS_HAT0Y = %i\n", ABS_HAT0X, ABS_HAT0Y);
	printf("ABS_HAT1X = %i\nABS_HAT1Y = %i\n", ABS_HAT1X, ABS_HAT1Y);
*/
