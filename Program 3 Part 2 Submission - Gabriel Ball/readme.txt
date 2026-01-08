Configuration used:

Compiler: Microsoft Visual C++ Compiler
IDE: Visual Studio
Language: C++
Environment: Windows 10
Ran server in Visual Studio and clients in command prompt

How to run clients in Command Prompt:
Change directory to location of .exe and include inputFile in same directory -> run by entering "ClientP3.exe -a 127.0.0.1 -p port -f inputFile.txt"

How to run server in Visual Studio:
Project Properties -> Debugging -> Command Arguments: Add "-p port" -> Apply -> OK -> Run