# ESP32-CAM Access Control System

Forked from https://github.com/robotzero1/esp32cam-access-control<br>
With more info at https://robotzero.one/access-control-with-face-recognition/<br>
Thank you Robotzero for making this available.

I wasn't aiming to make an access contol system, but did want to experiment with what the ESP32-CAM can do in terms of its built in facial recognition and image processing.

When ESP32 is installed under the Arduino environment, there are very simular example projects, but I chose this project to fork, because it worked first time, and had a solution that didn't require editing of partitions.
The repository I forked says 
  Note: If you are using release 1.0.5 of the ESP32 Arduino Hardware Libraries you can now run the entire project with just these files:  
  FaceDoorEntryESP32Cam.ino  
  camera_index.h  
  camera_pins.h  
  partitions.csv  


#Step 1<br>
First I compiled the code and ran it unchnged.  I used a cheap module with 4M PSRAM, and a separate board that provided the USB to serial.
I have used the ESP32-CAM before and it is very prone to brownout, so I attached a nice electrolytic capacitor accross the 3.3v and added an additional pair of wires from the 5v to a USB plug. This provide additional power during programming, and an alternate way of powering the board when not programming.<br>
There are plenty of sites out there that explain the brownout problem.<br>
I would recoment the module that provides the programing interface, instead of a separate USB to 3.3v serial, as it also handles putting the device in to programming mode, and there is no need to make a connection between two pins each time.

I have left the web interface unchanged for now, but intend to add scripts to zip and convert to hex the web page, making it easier to update.
![Interface](https://robotzero.one/wp-content/uploads/bfi_thumb/featured-master-1-6qb32y1978bm9yf6ne9nypfey83yf3pvisk7rozvfi8.jpg)  

The result worked, and looked great until it was asked to perform the recognition, then it got very slow, but then you would expect this from a small and cheap processor.  However I wanted more, so I started to change the code.

#Step 2<br>
The low hanging fruit.  I noticed that my device was running at only 80MHz when it was able to run at 240MHz.
Adding a very small amount of code made it switch into 240MHz and display the selected speed.  Three times faster is good for just a small edit.  I have no idea if other modules default to 240MHz, or if the code was left with the 80MHz to make it compatible with more devices.  I'm afriad I just wanted mine to work well, so I haven't performed any testing with other types of units.

#Step 3<br>
So the next observation was that the ESP32 is a dual core device, and the code is clearly using only one.
What if I could run the loop that collected frames and passed them to the web page, on one core.  While performing the slow recognition stuff on the other.  There might be a little latency, but that wouldn't matter too much as long as the web page appeared to be running normally.

First I created some functions to interact with shared resources, such as the serial port and sockets in the web page link.  Each of these was locked with a semaphore, so that it could only be entered by a single thread at a time.  I chose to use just one semaphore for the whole resource de-confliction, because with just 2 threads the chance of gaining significant performance by using more, was quite small.
I imediatly found problems with the function that handles requests from the web page.  I chose to separete the responses from the requests, by storing the strings to be sent in variables, and handeling them in my main processing thread.  Its a bit clunky but was an easy change.  Maybe I will re-write this at some point, but it would only make the code tidier, and would not have a performance gain.

I created queues as a sinple mechanism of passing control to the recognition thread when it was ready to receive a new frame, and for knowing when the frame had been used, to continue the main thread.  I may have made this a little more complicated than required, but I had originally considered passing a stream of frames.

I created a second thread on the second core, that was triggered by the queue and performed all of the recognition processing.
Lastly I still had some unreliability.  Although it improved when I scrificed some performance and gaurded most of the recognition code with the semaphore, this was not the real problem.
I downloaded a tool that allowed me to examine the backtrace after the crash, and the problem turned out to be in the wensockets send of the jpeg image to the webpage.  The send routine took a copy of the whole jpeg image in a string befor sending.  Chaning this to two sends, one for the header and one for the jpeg, made the problem go away.

I am quite pleased with the performance now.  The camera is still cheap and needs good light.  The face recognition is not what you wuld have on a high security system, or even a mobile phone, but it does work.  There were a number of other features that I meant to add, but haven't yet, such as adding face recognition boxes to the imaage in the web page.  The unreliability both from the brown outs and the potential memory problems in the two threads appear to be under control.  The code runs a lot faster than it did.

Have fun, and let me know if you try it and it works (or not).
