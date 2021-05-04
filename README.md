This program is compiled using make. The executable to run the program is ./oss . The timing is different in is program so that user processes would spawn and disk read/writes would take
place. Also I needed to reduced the total memory from 256K to 64K so that it would limit the amount of memory and cause more page faults. In addition, I had to change the total number of 
user processes to 25 unstead of 40 this is beacuse the system would fail when too many childern would spawn I've encoutered this error before and noted it in assignments before. `
