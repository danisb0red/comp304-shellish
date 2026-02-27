#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <termios.h> // termios, TCSANOW, ECHO, ICANON
#include <unistd.h>
#include <fcntl.h> 
#include <dirent.h> 
const char *sysname = "shellish";

enum return_codes {
  SUCCESS = 0,
  EXIT = 1,
  UNKNOWN = 2,
};

struct command_t {
  char *name;
  bool background;  // this shows if the command should run in background or not ...boolean
  bool auto_complete;
  int arg_count;
  char **args;
  char *redirects[3];     // in/out redirection
  struct command_t *next; // for piping
};

/**
 * Prints a command struct
 * @param struct command_t *
 */
void print_command(struct command_t *command) {
  int i = 0;
  printf("Command: <%s>\n", command->name);
  printf("\tIs Background: %s\n", command->background ? "yes" : "no");
  printf("\tNeeds Auto-complete: %s\n", command->auto_complete ? "yes" : "no");
  printf("\tRedirects:\n");
  for (i = 0; i < 3; i++)
    printf("\t\t%d: %s\n", i,
           command->redirects[i] ? command->redirects[i] : "N/A");
  printf("\tArguments (%d):\n", command->arg_count);
  for (i = 0; i < command->arg_count; ++i)
    printf("\t\tArg %d: %s\n", i, command->args[i]);
  if (command->next) {
    printf("\tPiped to:\n");
    print_command(command->next);
  }
}

/**
 * Release allocated memory of a command
 * @param  command [description]
 * @return         [description]
 */
int free_command(struct command_t *command) {
  if (command->arg_count) {
    for (int i = 0; i < command->arg_count; ++i)
      free(command->args[i]);
    free(command->args);
  }
  for (int i = 0; i < 3; ++i)
    if (command->redirects[i])
      free(command->redirects[i]);
  if (command->next) {
    free_command(command->next);
    command->next = NULL;
  }
  free(command->name);
  free(command);
  return 0;
}

/**
 * Show the command prompt
 * @return [description]
 */
int show_prompt() {
  char cwd[1024], hostname[1024];
  gethostname(hostname, sizeof(hostname));
  getcwd(cwd, sizeof(cwd));
  printf("%s@%s:%s %s$ ", getenv("USER"), hostname, cwd, sysname);
  return 0;
}

/**
 * Parse a command string into a command struct
 * @param  buf     [description]
 * @param  command [description]
 * @return         0
 */
int parse_command(char *buf, struct command_t *command) {
  const char *splitters = " \t"; // split at whitespace
  int index, len;
  len = strlen(buf);
  while (len > 0 && strchr(splitters, buf[0]) != NULL) // trim left whitespace
  {
    buf++;
    len--;
  }
  while (len > 0 && strchr(splitters, buf[len - 1]) != NULL)
    buf[--len] = 0; // trim right whitespace

  if (len > 0 && buf[len - 1] == '?') // auto-complete
    command->auto_complete = true;
  if (len > 0 && buf[len - 1] == '&') // background  ...if it ends with & then its a background process
    command->background = true;

  char *pch = strtok(buf, splitters);
  if (pch == NULL) {
    command->name = (char *)malloc(1);
    command->name[0] = 0;
  } else {
    command->name = (char *)malloc(strlen(pch) + 1);
    strcpy(command->name, pch);
  }

  command->args = (char **)malloc(sizeof(char *));

  int redirect_index;
  int arg_index = 0;
  char temp_buf[1024], *arg;
  while (1) {
    // tokenize input on splitters
    pch = strtok(NULL, splitters);
    if (!pch)
      break;
    arg = temp_buf;
    strcpy(arg, pch);
    len = strlen(arg);

    if (len == 0)
      continue; // empty arg, go for next
    while (len > 0 && strchr(splitters, arg[0]) != NULL) // trim left whitespace
    {
      arg++;
      len--;
    }
    while (len > 0 && strchr(splitters, arg[len - 1]) != NULL)
      arg[--len] = 0; // trim right whitespace
    if (len == 0)
      continue; // empty arg, go for next

    // piping to another command
    if (strcmp(arg, "|") == 0) {
      struct command_t *c =
          (struct command_t *)malloc(sizeof(struct command_t));
      int l = strlen(pch);
      pch[l] = splitters[0]; // restore strtok termination
      index = 1;
      while (pch[index] == ' ' || pch[index] == '\t')
        index++; // skip whitespaces

      parse_command(pch + index, c);
      pch[l] = 0; // put back strtok termination
      command->next = c;
      continue;
    }

    // background process
    if (strcmp(arg, "&") == 0)
      continue; // handled before

    // handle input redirection
    redirect_index = -1;
    if (arg[0] == '<')
      redirect_index = 0;
    if (arg[0] == '>') {
      if (len > 1 && arg[1] == '>') {
        redirect_index = 2;
        arg++;
        len--;
      } else
        redirect_index = 1;
    }
    if (redirect_index != -1) {
      command->redirects[redirect_index] = (char *)malloc(len);
      strcpy(command->redirects[redirect_index], arg + 1);
      continue;
    }

    // normal arguments
    if (len > 2 &&
        ((arg[0] == '"' && arg[len - 1] == '"') ||
         (arg[0] == '\'' && arg[len - 1] == '\''))) // quote wrapped arg
    {
      arg[--len] = 0;
      arg++;
    }
    command->args =
        (char **)realloc(command->args, sizeof(char *) * (arg_index + 1));
    command->args[arg_index] = (char *)malloc(len + 1);
    strcpy(command->args[arg_index++], arg);
  }
  command->arg_count = arg_index;

  // increase args size by 2
  command->args = (char **)realloc(command->args,
                                   sizeof(char *) * (command->arg_count += 2));

  // shift everything forward by 1
  for (int i = command->arg_count - 2; i > 0; --i)
    command->args[i] = command->args[i - 1];

  // set args[0] as a copy of name
  command->args[0] = strdup(command->name);
  // set args[arg_count-1] (last) to NULL
  command->args[command->arg_count - 1] = NULL;

  return 0;
}

void prompt_backspace() {
  putchar(8);   // go back 1
  putchar(' '); // write empty over
  putchar(8);   // go back 1 again
}

/**
 * Prompt a command from the user
 * @param  buf      [description]
 * @param  buf_size [description]
 * @return          [description]
 */
int prompt(struct command_t *command) {
  int index = 0;
  int c;
  char buf[4096];
  static char oldbuf[4096];

  // tcgetattr gets the pagitrameters of the current terminal
  // STDIN_FILENO will tell tcgetattr that it should write the settings
  // of stdin to oldt
  static struct termios backup_termios, new_termios;
  tcgetattr(STDIN_FILENO, &backup_termios);
  new_termios = backup_termios;
  // ICANON normally takes care that one line at a time will be processed
  // that means it will return if it sees a "\n" or an EOF or an EOL
  new_termios.c_lflag &=
      ~(ICANON |
        ECHO); // Also disable automatic echo. We manually echo each char.
  // Those new settings will be set to STDIN
  // TCSANOW tells tcsetattr to change attributes immediately.
  tcsetattr(STDIN_FILENO, TCSANOW, &new_termios);

  show_prompt();
  buf[0] = 0;
  while (1) {
    c = getchar();
    // printf("Keycode: %u\n", c); // DEBUG: uncomment for debugging
     if (c == EOF) {
    tcsetattr(STDIN_FILENO, TCSANOW, &backup_termios);
    return EXIT;
  }
  

    if (c == 9) // handle tab
    {
      buf[index++] = '?'; // autocomplete
      break;
    }

    if (c == 127) // handle backspace
    {
      if (index > 0) {
        prompt_backspace();
        index--;
      }
      continue;
    }

    if (c == 27 || c == 91 || c == 66 || c == 67 || c == 68) {
      continue;
    }

    if (c == 65) // up arrow
    {
      while (index > 0) {
        prompt_backspace();
        index--;
      }

      char tmpbuf[4096];
      printf("%s", oldbuf);
      strcpy(tmpbuf, buf);
      strcpy(buf, oldbuf);
      strcpy(oldbuf, tmpbuf);
      index += strlen(buf);
      continue;
    }

    putchar(c); // echo the character
    buf[index++] = c;
    if (index >= sizeof(buf) - 1)
      break;
    if (c == '\n') // enter key
      break;
    if (c == 4) // Ctrl+D
      return EXIT;
  }
  if (index > 0 && buf[index - 1] == '\n') // trim newline from the end
    index--;
  buf[index++] = '\0'; // null terminate string

  strcpy(oldbuf, buf);

  parse_command(buf, command);

  print_command(command); // DEBUG: uncomment for debugging

  // restore the old settings
  tcsetattr(STDIN_FILENO, TCSANOW, &backup_termios);
  return SUCCESS;
}


char *find_path(char *command) {
  int i;
  char path_copy[1024];
  char full_path[1024];
  char *dir;

   for (i = 0; command[i] != '\0'; i++) {
    if (command[i] == '/') {
      if (access(command, X_OK) == 0)
        return strdup(command);
      return NULL;
    }
  }
  // get PATH 
  char *path_env = getenv("PATH");
  if (path_env == NULL)
    return NULL;
  strcpy(path_copy, path_env);

  // try each folder
  dir = strtok(path_copy, ":"); //splitting 
  while (dir != NULL) {
    strcpy(full_path, dir);
    strcat(full_path, "/");
    strcat(full_path, command);

    //  exist?
    if (access(full_path, X_OK) == 0)
      return strdup(full_path);

    dir = strtok(NULL, ":");
  }

  return NULL;
}

///ls -la | grep shellax | wc need to implement this by pipinng
/// so output of ls -la is input to grep and output of grep is input to wc
/// 

int process_command(struct command_t *command) {
  int r;
  if (strcmp(command->name, "") == 0)
    return SUCCESS;

  if (strcmp(command->name, "exit") == 0)
    return EXIT;

  if (strcmp(command->name, "cd") == 0) {
    if (command->arg_count > 0) {
      r = chdir(command->args[1]);
      if (r == -1)
        printf("-%s: %s: %s\n", sysname, command->name, strerror(errno));
      return SUCCESS;
    }
  }
  if (strcmp(command->name, "cut") == 0) {
  char delimiter = '\t';          
  int fields[100];    //store field indices for fields that should be cut          
  int numfields = 0;  // this will hav num of field thst should be cut
   for (int i = 1; i < command->arg_count - 1; i++) {
    if (strcmp(command->args[i], "-d") == 0 ) {
      if (command->args[i + 1] != NULL) {   //check if a delim was given
        delimiter = command->args[i + 1][0];  
        i++;  
      }
      // TODO: handle the full form of -d like delim and field no ?
       }
        else if (strncmp(command->args[i], "-d", 2) == 0 && strlen(command->args[i]) > 2) {
      delimiter = command->args[i][2];
    }
    else if (strcmp(command->args[i], "-f") == 0) {
      if (command->args[i + 1] != NULL) {
        char fieldscopy[256];
        strcpy(fieldscopy, command->args[i + 1]);
        
        char *token = strtok(fieldscopy, ",");
        while (token != NULL && numfields < 100) {
          fields[numfields++] = atoi(token);  // convert str to int
          token = strtok(NULL, ",");
        }
        i++; 
      }
    }
     else if (strncmp(command->args[i], "-f", 2) == 0 && strlen(command->args[i]) > 2) {
      char fieldscopy[256];
      strcpy(fieldscopy, command->args[i] + 2);  // skip "-f"
      char *token = strtok(fieldscopy, ",");
      while (token != NULL && numfields < 100) {
        fields[numfields++] = atoi(token);
        token = strtok(NULL, ",");
      }
    }
  }
   char line[4096];
  char delim_str[2] = {delimiter, '\0'};  // strtok needs a string

  while (fgets(line, sizeof(line), stdin) != NULL) {
    line[strcspn(line, "\n")] = '\0';

    char *tokens[100];
    int count = 0;
    
    char linecopy[4096];
    strcpy(linecopy, line); 
    
    char *tok = strtok(linecopy, delim_str);
    while (tok != NULL) {
      tokens[count++] = tok;
      tok = strtok(NULL, delim_str);
    }
    for (int i = 0; i < numfields; i++) {
      int index = fields[i] - 1;  
      if (index >= 0 && index < count) {
        printf("%s", tokens[index]);
        if (i != numfields - 1) {
          printf("%c", delimiter);  // delimiter between fields
        }
      }
    }
    printf("\n");  
  }

  return SUCCESS;
}


/// idea for chatrooom
/// check if room exists..if it doesnt create a folder/directory
/// check if user exists....if not then create a named text file (pipe)
/// two cases : 1) send message -> wrtie to all files in the room
///              2) read message -> read from the user file 

 if (strcmp(command->name, "chatroom") == 0) {  //first arg is roomname and second is username
  char* roomname = command->args[1];
  char* username = command->args[2];
  char roompath[1024];
  snprintf(roompath, sizeof(roompath), "/tmp/chatroom-%s", roomname);
    if (access(roompath, F_OK) == -1) {
        mkdir(roompath, 0755); // create room directory if not exists
    }
    
    char userpipe[1024];
    snprintf(userpipe,sizeof(userpipe), "/tmp/chatroom-%s/%s", roomname, username);
    if (access(userpipe, F_OK) == -1) {
        mkfifo(userpipe, 0777); }
        // Case 2: read messages
        pid_t pid = fork();
        if (pid == 0) { // child process for reading 
            char readbuf[1024];
            while (1) { //infinite loop ...!! Process keeps running always
                int fd = open(userpipe, O_RDONLY);
                if (fd < 0) {
                    printf("Error opening user pipe for reading.\n");
                    exit(1); }
                int n = read(fd, readbuf, sizeof(readbuf) - 1);
                if (n > 0) {
                    readbuf[n] = '\0';
                    printf("\n%s\n", readbuf); 
                    printf("[%s] %s > ", roomname, username);
                    fflush(stdout);
                }
                close(fd);
            }
        }
       
        // Case 1: send messages
        char sendbuf[1024];
        printf("[%s] %s > ", roomname, username);
        fflush(stdout);
        while (fgets(sendbuf, sizeof(sendbuf), stdin) != NULL) { 
            sendbuf[strcspn(sendbuf, "\n")] = '\0'; // remove newline
            char message[1100];
            snprintf(message, sizeof(message), "[%s] %s: %s", roomname, username, sendbuf);
            // Write message to all user pipes in the room
            char userfile[1024];
            DIR *d = opendir(roompath);
            if (d) {
                struct dirent *dir;
                while ((dir = readdir(d)) != NULL) {
                     // only write to named pipes
                         if (dir->d_name[0] == '.') continue; // we skip files like . and .. cuz they are not pipes
                         if (strcmp(dir->d_name, username) == 0) continue; // dont write to own pipe !!
                        snprintf(userfile, sizeof(userfile), "%s/%s", roompath, dir->d_name); 
                        int fd = open(userfile, O_WRONLY | O_NONBLOCK);
                        if (fd >= 0) {
                            write(fd, message, strlen(message));
                            close(fd); }
                     }
                     closedir(d);
                  }  
            printf("[%s] %s > ", roomname, username);
            fflush(stdout);
        }
       
              return SUCCESS;
 }

 /// idea fo custom command... a riddle  like chatroom
 /// so its a game ?? where therre is one host who submits the riddle and the answer and the numb of tries
 /// then players (multiple if i have enough time) can answer
 /// format riddle <host/player>
 if (strcmp(command->name, "riddle") == 0) {
   if (command->arg_count < 2) {
    printf("Usage: riddle <host/player>\n");
    return SUCCESS;
  }

  char* role = command->args[1];

  //remove these comments !!!!! TODO

  // what if the host can keep giving riddls so  its a fifo ..
  // need to fork so that host can keep giving riddles and players can keep playing without exiting the program
  // ok so host job is to submit the riddle and also validate the answer..so host doesnt ig send the answer in the fifo it only sends the riddle
  /// ok think in file terms..
  /// host creates a fifo file that has the riddle 
  /// number of tris and the answer in a variable in the host process
  /// players read from the fifo file and submit their answers in another fifo file for the host to read and validate
  /// if the answer is correct , host process writes a message in the fifo file for the player to read and know that they won
  /// if the answer is wrong and they have tries left , host process writes a message in
  /// ok so in simple terms..2 fifo files
  /// 3 cases : 1) host creates the riddle fifo
  ///          2) player reads the riddle fifo and submits answer in answer fifo
  ///          3) host reads the answer fifo and validates and writes result in result fifo for player to read

  if (strcmp(role, "host") == 0) {

    char riddlefifo[] = "/tmp/riddle_fifo";
    char answerfifo[] = "/tmp/answer_fifo";
    char resultfifo[] = "/tmp/result_fifo";

    mkfifo(riddlefifo, 0666);
    mkfifo(answerfifo, 0666);
    mkfifo(resultfifo, 0666);

    char riddle[1024];
    char answer[1024];
    int tries;

    printf("Enter the riddle: ");
    fgets(riddle, sizeof(riddle), stdin);
    riddle[strcspn(riddle, "\n")] = '\0';

    printf("Enter the answer: ");
    fgets(answer, sizeof(answer), stdin);
    answer[strcspn(answer, "\n")] = '\0';

    printf("Enter the number of tries: ");
    scanf("%d", &tries);
    getchar(); 
     printf("Riddle sent. Waiting...\n");
    // write riddle to riddlefifo for players to read
    int rfd = open(riddlefifo, O_WRONLY);
    write(rfd, riddle, strlen(riddle));
    close(rfd);

   

    while (tries > 0) {

        char playeranswer[1024];

        int afd = open(answerfifo, O_RDONLY);  
        int n = read(afd, playeranswer, sizeof(playeranswer) - 1);
        close(afd);

        if (n > 0) {
            playeranswer[n] = '\0';

            printf("Received guess: %s\n", playeranswer);

            int resfd = open(resultfifo, O_WRONLY);

            if (strcmp(playeranswer, answer) == 0) {

                char winmsg[1024];
                snprintf(winmsg, sizeof(winmsg),
                         "Correct! The answer is: %s", answer);

                write(resfd, winmsg, strlen(winmsg));
                close(resfd);

                printf("Player guessed correctly. Game over.\n");
                break;

            } else {

                tries--;

                char wrongmsg[1024];

                if (tries > 0) {
                    snprintf(wrongmsg, sizeof(wrongmsg),
                             "Wrong answer. Tries left: %d", tries);
                } else {
                    snprintf(wrongmsg, sizeof(wrongmsg),
                             "Game over! The correct answer was: %s", answer);
                }

                write(resfd, wrongmsg, strlen(wrongmsg));
                close(resfd);

                printf("Wrong guess. Remaining tries: %d\n", tries);
            }
        }
    }

    unlink(riddlefifo);
    unlink(answerfifo);
    unlink(resultfifo);

    return SUCCESS;

  } else if (strcmp(role, "player") == 0) {

    char riddlefifo[] = "/tmp/riddle_fifo";
    char answerfifo[] = "/tmp/answer_fifo";
    char resultfifo[] = "/tmp/result_fifo";

    char riddle[1024];
    char answer[1024];

    int rfd = open(riddlefifo, O_RDONLY);
    if (rfd < 0) {
        printf("No riddle available. Error.\n");
        return SUCCESS;
    }

    int n = read(rfd, riddle, sizeof(riddle) - 1);
    close(rfd);

    if (n > 0) {

        riddle[n] = '\0';
        printf("Riddle: %s\n", riddle);

        while (1) {

            printf("Your answer: ");
            fgets(answer, sizeof(answer), stdin);
            answer[strcspn(answer, "\n")] = '\0';

            int afd = open(answerfifo, O_WRONLY);
            write(afd, answer, strlen(answer));
            close(afd);

            char result[1024];

            int resfd = open(resultfifo, O_RDONLY);
            n = read(resfd, result, sizeof(result) - 1);
            close(resfd);

            if (n > 0) {
                result[n] = '\0';
                printf("%s\n", result);

                if (strncmp(result, "Correct!", 8) == 0 ||
                    strncmp(result, "Game over!", 10) == 0) {
                    break;
                }
            }
        }

    } else {
        printf("No riddle available. Error.\n");
    }

    return SUCCESS;
  }

  return SUCCESS;}

  // PIPPIN CODE 
if (command->next != NULL) {
    int pipefd[2];
    pipe(pipefd);  

    // 2 childs...one for first comand and one for next/rest
    pid_t pid1 = fork();
    if (pid1 == 0) {
        close(pipefd[0]);          
        dup2(pipefd[1], STDOUT_FILENO);  // output of first commandis going to the pipe
        close(pipefd[1]);
        char *path = find_path(command->name); //find the command to call it
        if (path == NULL) {
            printf("-%s: %s: command not found\n", sysname, command->name);
            exit(127);
        }
        execv(path, command->args); //call the command 
        exit(127);
    }

    pid_t pid2 = fork();
    if (pid2 == 0) {
        close(pipefd[1]);           
        dup2(pipefd[0], STDIN_FILENO);   // Input of second command is from output of first command
        close(pipefd[0]);

        // Recursion 
        process_command(command->next);
        exit(0);
    }
    close(pipefd[0]);
    close(pipefd[1]);
    waitpid(pid1, NULL, 0);
    waitpid(pid2, NULL, 0);

    return SUCCESS;
}

  

  pid_t pid = fork();
  if (pid == 0) // child
  {
    /// This shows how to do exec with environ (but is not available on MacOs)
    // extern char** environ; // environment variables
    // execvpe(command->name, command->args, environ); // exec+args+path+environ

    /// This shows how to do exec with auto-path resolve
    // add a NULL argument to the end of args, and the name to the beginning
    // as required by exec

    // TODO: do your own exec with path resolving using execv()
    // do so by replacing the execvp call below
    //execvp(command->name, command->args); // exec+args+path 
    // need to find where is the location of the command->name 
    //then pass it to execv first parameter 
     if (command->redirects[0] != NULL) {
      int fd = open(command->redirects[0],O_RDONLY);
      if (fd<0) {
        printf("Error opening file.");
      }
      dup2(fd,0);
      close(fd);
     }
     if (command->redirects[1] != NULL) {
      int fd = open(command->redirects[1],O_WRONLY | O_CREAT | O_TRUNC, 0644);
      if (fd<0) {
        printf("Error opening file.");
      }
      dup2(fd,1);
      close(fd);
     }
    if (command->redirects[2] != NULL) {
      int fd = open(command->redirects[2], O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (fd < 0) {
      printf("Error opening file.");
    }
    dup2(fd, 1); 
    close(fd);
  }
     char *path = find_path(command->name);
     
     if (path == NULL) {
      printf("-%s: %s: command not found\n", sysname, command->name);
      exit(127);
    }
    execv(path, command->args);
      printf("-%s: %s: %s\n", sysname, command->name, strerror(errno));
      exit(127);


    ////////  trying to implement CUT !!!
     /// assuming i have the string as a single line ... called line_str
    /// and i have the delimiter as delimtemp 
    /// and i have field indices as an array 
    /// now write code to do the cut command thingy
 //   char *sepstr = strtok(line_str, delimtemp); 
   // count = 0;
   // while (sepstr != NULL) {
   //     tokens[count++] = sepstr;
 //       sepstr = strtok(NULL, delimtemp);     
   // }
   // int total = count;
    //for (int i = 0; i < numfields; i++) {
    //  int index = fields[i] - 1; 
     // if (index < total) {
       //   printf("%s", tokens[index]);
         // if (i != numfields - 1)
           //   printf("%c", delimtemp[0]);
    //  }
 // }
  printf("\n");
  } else {
    // TODO: implement background processes here
       if (command->background) {
	       printf("[%d]\n",pid); //print pid like in a normal shell terminal and keep going
       } else{
	       waitpid(pid, NULL, 0);
       } // wait for child if its not a background process
    return SUCCESS;
  }
}

int main() {
  while (1) {
    struct command_t *command =
        (struct command_t *)malloc(sizeof(struct command_t));
    memset(command, 0, sizeof(struct command_t)); // set all bytes to 0

    int code;
    code = prompt(command);
    if (code == EXIT)
      break;

    code = process_command(command);
    if (code == EXIT)
      break;

    free_command(command);
  }

  printf("\n");
  return 0;
}
