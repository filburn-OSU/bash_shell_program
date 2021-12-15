// Author: Mike Filburn
// Date: 7/30/21
// Resources:
// https://www.ibm.com/docs/en/zos/2.1.0?topic=functions-sigaction-examine-change-signal-action
// https://www.geeksforgeeks.org/dup-dup2-linux-system-call/
// https://man7.org/linux/man-pages/man2/dup.2.html
// https://cboard.cprogramming.com/c-programming/90136-file-descriptor-redirection.html
// https://stackoverflow.com/questions/14846768/in-c-how-do-i-redirect-stdout-fileno-to-dev-null-using-dup2-and-then-redirect
// https://pubs.opengroup.org/onlinepubs/7908799/xsh/open.html
// https://www.geeksforgeeks.org/input-output-system-calls-c-create-open-close-read-write/
// https://www.cplusplus.com/reference/cstring/
// http://www.cplusplus.com/reference/cstring/memmove/
// https://www.cyberciti.biz/faq/kill-process-in-linux-or-terminate-a-process-in-unix-or-linux-systems/
// https://www.guru99.com/linux-redirection.html
// https://stackoverflow.com/questions/47024848/how-does-waitpidpid-t-1-null-wnohang-keep-track-of-child-processes-to-be
// https://phoenixnap.com/kb/linux-file-permissions
// https://man7.org/linux/man-pages/man2/getpid.2.html
// https://pubs.opengroup.org/onlinepubs/009695399/functions/fflush.html
// https://www.cyberciti.biz/faq/linux-unix-sleep-bash-scripting/
// https://stackoverflow.com/questions/9147760/how-to-get-child-pid-in-c
// https://www.folkstalk.com/2012/07/kill-command-examples-in-unix-linux.html
// https://www.qnx.com/developers/docs/6.5.0SP1.update/com.qnx.doc.neutrino_lib_ref/e/execv.html
// https://pubs.opengroup.org/onlinepubs/009695399/functions/getppid.html
// https://www.tutorialspoint.com/c_standard_library/c_function_scanf.htm







#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>
#include <sys/stat.h>
#include <fcntl.h>


//Main struct. I decided against having global variables because I like using mainInput-> before using a variable. Helps visualisation of what I am doing.
struct mainInput_
{   
    //Loop Information - setup each loop
    //this just holds the input
    char commandIn[2048 + 1]; //+1 for null character.

    //use the command for quick reference and readability. Still in arguments array.
    char command[100];
    
    //holds the parameters and how many parameters are loaded.
    char *arguments[512 + 1];
    int argumentsLength;

    //holds the child processes and the length of how many child processes.
    int childProcessIDs[500];
    int childProcessLength;

    //keeps track of if a foreground or background was found at the end of the input string.
    int foreground;

    //keeps track of the last closed foreground status. Needed fot 'status' portion.
    int foregroundStatus;

    //string for the filename to divert stream to.
    char inFileName[50];

    //string for the filename to divert the stream from.
    char outFileName[50];

} ;

// function prototypes
void fillCommandStructure(struct mainInput_ *mainInput);
void checkCD(struct mainInput_ *mainInput);
void checkStatus(struct mainInput_ *mainInput);
void checkExit(struct mainInput_ *mainInput);
void checkCMD(struct mainInput_ *mainInput);
void fillCommandStructureSetup(struct mainInput_ *mainInput);
void resetCommandStructure(struct mainInput_ *mainInput);
void removeChildProcessIDs(struct mainInput_ *mainInput, int childProcessIDs);
void foreGroundCheck(struct mainInput_ *mainInput);
void processEndedCheck(struct mainInput_ *mainInput);
void statusCheck(struct mainInput_ *mainInput);
void checkExpansionOfVariables(struct mainInput_ *mainInput);
void Ctrl_ZToggle();
void clear();

//Global Variable for ctrl-z and ctrl-c
int Ctrl_ZGlobal = 0;
struct sigaction Ctrl_C = {{0}};




int main(int argc, char *argv[])
{   
    struct mainInput_ mainInput;
    fillCommandStructureSetup(&mainInput);
   while(1){
        fillCommandStructure(&mainInput);
        if(mainInput.argumentsLength >0)
        {
            foreGroundCheck(&mainInput);
            checkExpansionOfVariables(&mainInput);
            checkCD(&mainInput); 
            checkCMD(&mainInput); 
            checkStatus(&mainInput);
            checkExit(&mainInput);
        }
        resetCommandStructure(&mainInput);
    }
}





//============================================= Functions ================================================================

//check expansion. changes $$ to the pid.
void checkExpansionOfVariables(struct mainInput_ *mainInput)
{   
    //go through all the parced input arguments
    for (int i = 0; i < mainInput->argumentsLength; i++)
    {   
        //create a temp variable. It is used to hold the address of the element found using strstr.
        char *temp = "";

        while(temp != NULL)
        {

            temp = strstr(mainInput->arguments[i], "$$");

            if(temp != NULL)
            {
                char newString[100];

                //temp string to hold the integer when converted.
                char pidStr[20];
                //convert int to string using sprintf.
                sprintf( pidStr, "%d", getpid() );clear();
                //create the string by combining pieces in newString.
                memmove(newString, mainInput->arguments[i], strlen(mainInput->arguments[i]) - strlen(temp) );
                memmove(newString + ( strlen(mainInput->arguments[i]) - strlen(temp) ) , pidStr, strlen(pidStr) );
                memmove(newString + ( strlen(mainInput->arguments[i]) - strlen(temp) + strlen(pidStr) ), temp + 2, strlen(temp) - 2 );
                //overwrite the origional string.
                strcpy(mainInput->arguments[i], newString);
            }
        }
    }
}




// local function Status. Just 
void checkStatus(struct mainInput_ *mainInput)
{
    if(strcmp(mainInput->command, "status") == 0)
    {   
        //uses macro WIFEXITED
        if(WIFEXITED(mainInput->foregroundStatus))
        {   //the actual exit value printed to the user
            printf("exit value %d\n", WEXITSTATUS(mainInput->foregroundStatus));clear();
        }
        //uses macro WIFSIGNALED
        else
        {   //the actual signal that is printed to the user.
            printf("terminated by signal %d\n", WTERMSIG(mainInput->foregroundStatus));clear();
        }
    }
}



// this is ran in the event an input is given and it is not a local command. This is kind of the pass through command. It copies itself using fork. It gets a new
// PID. If all goes well it will open the first argument as its own process. If not it will error with an error message.
void checkCMD(struct mainInput_ *mainInput)
{   
    if(strcmp(mainInput->command, "exit") != 0 &&
    strcmp(mainInput->command, "cd") != 0 &&
    strcmp(mainInput->command, "status") != 0 &&
    strcmp(mainInput->command, "") != 0)
    {
    
        //set the process id to something negative and not -1 since it may be returned.
        int createdChildResult = -2;
        int childPID;

        //create a child process.
        createdChildResult = fork();

        switch(createdChildResult)
        {
            //====== ERROR CASE ===========
            case -1 : ; 
            printf("Error could not create child 'fork()' #%i\n", mainInput->childProcessLength);clear(); 
            exit(1); 
            break;
            

            //======= Childs Work ============
            case 0  : ; 

                //check for '<' and '>'. The filename coorilating with the < or > has already been filtered and stored by the fillCommandStructure()
                if(strcmp(mainInput->inFileName, "") != 0) // write out to file ie. 'ls > junk'
                {
                    int fileOpen = open(mainInput->inFileName, O_WRONLY | O_CREAT | O_TRUNC, 0666);
                    if (fileOpen < 0)
                    {
                        printf("\ncannot open %s for output\n", mainInput->inFileName);clear();
                        exit(1);
                    }
                    else
                    {
                        dup2(fileOpen, 1);
                        close(fileOpen);
                    }
                }

                if(strcmp(mainInput->outFileName, "") != 0) // write out to file ie. 'wc < junk'
                {
                    int fileOpen = open(mainInput->outFileName, O_RDONLY);
                    if (fileOpen < 0)
                    {
                        printf("\ncannot open %s for input\n", mainInput->outFileName);clear();
                        exit(1);
                    }
                    else
                    {
                        dup2(fileOpen, 0);
                        close(fileOpen);
                    }
                }

                //below is ran in the event a we are in background mode. needs an & at the end and we can not be in foreground only mode (ctrl+z)
                if(mainInput->foreground == 1 && Ctrl_ZGlobal == 0)
                {            
                    //check to see if the ending argument is a &. if it is omit if from the arguments to be passed to execv.
                    //running in background mode.
                    //set the ending element in my struct arguments to null to be used with execv.
                    mainInput->arguments[mainInput->argumentsLength - 1] = NULL;
                    printf("background pid is %d\n", getpid());clear();
                    //divert stuff so it is trully running in background.
                    int devNull = open("/dev/null", O_RDWR);
                    dup2(devNull, 1);
                }
                else
                {
                    //if in foreground only mode, get rid of trailing &. 
                    if(mainInput->foreground == 1)
                    {
                        mainInput->argumentsLength--;
                    }

                    //set the ending element in my struct arguments to null to be used with execv.
                    //this is actually done twice but is a legacy instruction as part of the developement process.
                    //leaving just in case it affects something in an unexpected way.
                    mainInput->arguments[mainInput->argumentsLength] = NULL;

                    //re-enable ctrl+c so the foreground process can be killed again.
                    Ctrl_C.sa_handler = SIG_DFL;
                    sigaction(SIGINT, &Ctrl_C, NULL);
                }

                //run the program in using thie childs PID
                execvp(mainInput->command, mainInput->arguments);
                

                //if all goes well this below should never run. But just in case.
                printf("badfile: no such file or directory.\n");clear();

                exit(1);
                break;

            
            //========== Parents Work ===============
            //the parent program pretty much checks how to run it, foreground or background. fills the struct with returned information about the child process.
            default : ; 

                if (mainInput->foreground == 1 && Ctrl_ZGlobal == 0)//Run in back
                {
                    //store child process pid here.
                    mainInput->childProcessIDs[mainInput->childProcessLength] = createdChildResult;
                    mainInput->childProcessLength++;
                    //using WNOHANG since it is in background
                    createdChildResult = waitpid(createdChildResult, &childPID, WNOHANG);
                }
                else//Run in front
                { 
                    //using waitpid 0 since we are in foreground mode.
                    createdChildResult = waitpid(createdChildResult, &childPID, 0);
                    //store the status for status checking.
                    mainInput->foregroundStatus = childPID;
                    //print the status in the event the process was terminated.
                    if(!WIFEXITED(mainInput->foregroundStatus))
                    {
                        printf("terminated by signal %d\n", WTERMSIG(mainInput->foregroundStatus));clear();
                    }
                }

        }
        
    }

}




//sets the foreground variable in the main struct depending on if it should run in the background or foreground.
void foreGroundCheck(struct mainInput_ *mainInput)
{
    if(strcmp(mainInput->arguments[mainInput->argumentsLength - 1], "&") == 0)
    {
        mainInput->foreground = 1;
    }
    else
    {
        mainInput->foreground = 0;
    }
}




//just checks to see if the local process has been called. 
void checkExit(struct mainInput_ *mainInput)
{
    // The exit command exits the shell *done
    // kill any other processes or jobs that the shell has started.
    if(strcmp( mainInput->command, "exit") == 0)
    {
        //loop through and kill everything that is still running that smallsh started.
        for(int i = 0; i < mainInput->childProcessLength; i++)
        {
            //I chose -5 as my number to store when a process closes. I chose this since -1 and -2 were possibilities.
            if(mainInput->childProcessIDs[i] != -5)
            {
                kill(mainInput->childProcessIDs[i], SIGKILL);
            }    
        }
        exit(0);
    }
}




//get the full string in. fill the structure with the commans and arguments.
void fillCommandStructure(struct mainInput_ *mainInput)
{

        //reset the input
        strcpy(mainInput->commandIn, "");

        //printing all the things that closed. "just before the command prompt"
        processEndedCheck(mainInput);

        //used to make sure the prompt catches up and doesn't print to early.
        sleep(1);

        //Print Prompt
        printf("\n: ");clear();

        //Get user input
        fgets(mainInput->commandIn, 100, stdin);

        // setting null is needed since it adds a /n for some reason?
        mainInput->commandIn[strlen(mainInput->commandIn) - 1] = '\0';

        //loop while handling nothing entered.
        while(strlen(mainInput->commandIn) == 0)
        {   
            //printing all the things that closed. "just before the command prompt"
            processEndedCheck(mainInput);

            //Print Prompt
            printf("\n: ");clear();

            //get user input
            fgets(mainInput->commandIn, 100, stdin);

            // setting null is needed since it adds a /n for some reason?
            mainInput->commandIn[strlen(mainInput->commandIn) - 1] = '\0';
        }

        //get all the parameters entered out as strings and store them
        char *arg = strtok(mainInput->commandIn, " ");

        //a flag so I know when the first good string is found "ie: #foo is not good."
        int firstGoodCommandFound = 0;

        //way of keeping track of where we are in the while loop
        int iterator = 0;

        //loop through the entire input string and parse out into chunks of mini strings. checking for '<' '>' and '#'.
        while( arg != NULL ) 
        {
            //do the below to ignore the '#'
            if(arg[0] == '#')
            {
                arg = NULL;
            }
            //copy string into struct and move to correct position.
            else if(arg[0] == '<')
            {
                arg = strtok(NULL, " ");
                strcpy(mainInput->outFileName, arg);
                arg = strtok(NULL, " ");
            }
            //copy string into struct and move to correct position.
            else if(arg[0] == '>')
            {
                arg = strtok(NULL, " ");
                strcpy(mainInput->inFileName, arg);
                arg = strtok(NULL, " ");
            }
            //parse it and store it.
            else
            {
                //cpoy the first found as the command. did this for readability reasons.
                // mainInput->command is more intuitive than mainInput->arguments[0]
                if(firstGoodCommandFound == 0)
                {
                    //fill the struct command string for quick reference. It is still coppied to arguments for consistency.
                    strcpy(mainInput->command, arg);
                    firstGoodCommandFound = 1;
                }
                //store all, even the command for iterations.
                mainInput->arguments[iterator] = arg;
                arg = strtok(NULL, " ");
                iterator++;
            }
        }

    //write out an null at end of the array.
    mainInput->arguments[iterator + 1] = NULL; 
    
    //store in struct so we know how many strings exist in the array.
    mainInput->argumentsLength = iterator;
    clear();
}




//needed to clear out stuff every time we loop through and reset for next use. Not much to see here.
void resetCommandStructure(struct mainInput_ *mainInput)
{
    memset (mainInput->command, '\0', sizeof(mainInput->command) );
    memset (mainInput->commandIn, '\0', sizeof(mainInput->commandIn) );
    memset (mainInput->arguments, '\0', sizeof(mainInput->arguments) );

    mainInput->argumentsLength = 0;
    mainInput->foreground = 0;

    strcpy(mainInput->inFileName, "");
    strcpy(mainInput->outFileName, "");
}




//check to see if a process ended and display it.
void processEndedCheck(struct mainInput_ *mainInput)
{
    //check to see if any processes have quit.
    for(int i = 0; i < mainInput->childProcessLength; i++)
    {
        int   childStatus;
	    pid_t childPid = mainInput->childProcessIDs[i];
        //using WNOHANG since we dont want to hand. 0 if still running otherwise we will get something to show the user.
        childPid = waitpid(childPid, &childStatus,  WNOHANG);
        //things exited, more than likely normally.
        if(WIFEXITED(childStatus) && childPid != -1 && childPid != 0)
        {
			printf("background pid %d is done: exit value %d\n", childPid, WEXITSTATUS(childStatus));clear();
            mainInput->childProcessIDs[i] = -5;
		}
        //things did not exit normally.
        //could have put a else here, leaving as an if for readability reasons while learning. Not much lost.
        if(WIFSIGNALED(childStatus) && childPid != -1 && childPid != 0)
        {
			printf("background pid %d is done: terminated by signal %d\n", childPid, WTERMSIG(childStatus));clear();
            mainInput->childProcessIDs[i] = -5;
		}
    }
}




// used clear for readability reasons in code.
void clear ()
{   
    fflush(stdout);
}




//fills a few variables in the struct for inital use. Also sets up Ctrl+C and Ctrl+Z
void fillCommandStructureSetup(struct mainInput_ *mainInput)
{
    //handle Ctrl + C
    Ctrl_C.sa_handler = SIG_IGN; //change SIG_IGN to a function.
    Ctrl_C.sa_flags = 0;
    sigaction(SIGINT, &Ctrl_C, NULL);

    //handle Ctrl + Z
    struct sigaction Ctrl_Z = {{0}};
    Ctrl_Z.sa_handler = &Ctrl_ZToggle; //change SIG_IGN to a function.
    Ctrl_Z.sa_flags = 0;
    sigaction(SIGTSTP, &Ctrl_Z, NULL);

    //just a print out so we know that the shell has started.
    printf("$ smallsh\n");clear();

    //set all the variables to NULL so to prevent overrun.
    memset (mainInput->command, '\0', sizeof(mainInput->command) );
    memset (mainInput->commandIn, '\0', sizeof(mainInput->commandIn) );
    memset (mainInput->arguments, '\0', sizeof(mainInput->arguments) );
    memset (mainInput->childProcessIDs, '\0', sizeof(mainInput->childProcessIDs) );

    //initalize struct variables.
    mainInput->argumentsLength = 0;
    mainInput->childProcessLength = 0;
    mainInput->foreground = 0;
    mainInput->foregroundStatus = 0;

    //initalize struct strings to blank.
    strcpy(mainInput->inFileName, "");
    strcpy(mainInput->outFileName, "");
}




//check if local process CD has been entered.
void checkCD(struct mainInput_ *mainInput)
{
    if(strcmp(mainInput->command, "cd") == 0)
    {   
        //change working directory to home since user entered 'cd' only.
        if(mainInput->argumentsLength == 1)
        {
            chdir(getenv("HOME"));
        }
        //change the directory to what follows after the 'cd'
        else
        {
            chdir(mainInput->arguments[1]);
        }
    }
}



// handles Ctrl+Z. this is pretty much a toggle. Need to use write since printf is not an option. 
void Ctrl_ZToggle()
{
    if(Ctrl_ZGlobal == 0)
    {
        Ctrl_ZGlobal = 1;
        char *outMessage = "\nEntering Foreground-Only Mode (& is now ignored)\n";clear();
        write(1, outMessage, 50);
    }
    else
    {
        Ctrl_ZGlobal = 0;
        char *outMessage = "\nExiting Foreground-Only Mode\n";clear();
        write(1, outMessage, 30);
    }
}