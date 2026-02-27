# Shell-ish COMP 304


**Repository:** [https://github.com/danisb0red/comp304-shellish](https://github.com/danisb0red/comp304-shellish)

---

## 🚀 Running the Shell

```bash
./shell-ish
```

You will see :
```
username@hostname:/path/to/directory shellish$
```

---

##  Features


### Background Processes (`&`)
Run commands in the background:


### I/O Redirection
- `>` - Redirect output (overwrite)
- `>>` - Redirect output (append)
- `<` - Redirect input

```bash
shellish$ ls > files.txt
shellish$ echo "hello" >> log.txt
```

### Piping (`|`)

```bash
shellish$ ls -la | grep shell

```

---


## Custom Commands

### 1. `cut` 


**Usage:**
```bash
cut -d <delimiter> -f <fields>
```


**Examples:**
```bash

# Extract first and third fields
shellish$ echo "apple:banana:cherry:date" | cut -d ":" -f1,3
apple:cherry

```

---

### 2. `chatroom` 

A simple group chat application.

**Usage:**
```bash
chatroom <roomname> <username>
```

**How it works:**
- Creates a chat room folder at `/tmp/chatroom-<roomname>/`
- Each user gets a named pipe for receiving messages
- Messages are broadcast to all users in the room

**Example:**


**To exit:** Press `Ctrl+D`

---

### 3. `riddle` 

A two-player riddle game using named pipes.
One player acts as Host and sends a riddle with answer and number of tries.
The other player attempts to answer the riddle before the tries run out.

**Usage:**
```bash
riddle <host|player>
```

**How to play:**

*Terminal 1 (Host):*
```bash
shellish$ riddle host
Enter the riddle: Some riddle
Enter the answer: Some answer
Enter the number of tries: 3 or any number
Riddle sent. Waiting...
```

*Terminal 2 (Player):*
```bash
shellish$ riddle player
Riddle: the saved riddle
Your answer: x
Wrong answer. Tries left: 2
Your answer: y
Correct! The answer is: y
```

