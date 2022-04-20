# ESP32-CAM Access Control System

Forked from https://github.com/robotzero1/esp32cam-access-control
With more info at https://robotzero.one/access-control-with-face-recognition/
Thank you Robotzero for making this available.

I wasn't aiming to make am access contol system, but did want to experiment with what the ESP32-CAM can do in terms of its built in facial recognition and image processing.

There are very simular projects that are availble as example code when ESP32 is installed under the Arduino environment, but I chose this project to fork, because it worked first time, and had a solution that didn't require editing of partitions.
The forked repository says 
  Note: If you are using release 1.0.5 of the ESP32 Arduino Hardware Libraries you can now run the entire project with just these files:  
  FaceDoorEntryESP32Cam.ino  
  camera_index.h  
  camera_pins.h  
  partitions.csv  


#Step 1<br>
First I compiled the code and ran it unchnged.  I used a cheap module with 4M PSRAM, and a separate board that provided the USB to serial.
I have used the ESP32-CAM before and it is very prone to brownout, so I attached a nice electrolytic capacitor accross the 3.3v and added an additional pair of wires that ran to a USB plug, to provide additional power during programming, and an alternate way of powering the board when in use. There are plenty of sites out there that explain the brown out problem.  I would recoment the module that provides the programing interface over a normal USB to serial, as it also handles putting the device in to programming mode, and there is no need to make a connection between 2 pins each time.

I have left the web interface unchanged for now, but intend to add scripts to zip and convert to hex the web page, making it easier to update.
![Interface](https://robotzero.one/wp-content/uploads/bfi_thumb/featured-master-1-6qb32y1978bm9yf6ne9nypfey83yf3pvisk7rozvfi8.jpg)  

The result worked, and looked great until it was asked to perform the recognition, then it got very slow, but then you would expect this from a small and cheap processor.  However I wanted more, so I started to change the code.

#Step 2<br>
The low hanging fruit.  I noticed that my device was running at only 80MHz when it was able to run at 240MHz.
Adding a very small amount of code made it switch into 240MHz and display the selected speed.  Three times faster is good for just a small edit.  I have no idea if other modules default to 240MHz, or if the code was left with the 80MHz to make it compatible with more devices.  I'm afriad I just wanted mine to work well, so I haven't performed any testing with other types of units.

#Step 3<br>
So the next observation was that the ESP32 is a dual core device, and the code is clearly using only one.
What if I could run the loop that collected frames and passed them to the web page, on one core.  While performing the slow recognition stuff on the other.  There might be a little latency, but that wouldn't matter too much as long as the web page appeared to be running normally.

First I created some functiones to interact with shared resources, such as the serial port and sockets in the web page link.  Each of there was locked with a semaphore, so that it could only be entered by a single thread at a time.  I chose to use just one semaphore for the whole resource deconfliction, because with just 2 threads the chance of gaining significant performance by using more, was quite small.
I imediatly found problems with the function that handles requests from the web page.  I chose to separete the responses from the requests, by storing the strings to be sent in variables, and handeling them in my main processing thread.

I created queues as a sinple mechanism of passing control to the recognition thread when it was ready for a new frame, and for knowing when the frame had been used, and could receive a new one.  I may have made this a little more complicated than required, but I had originally considered passing a stream of frames.

I created a second thread on the second core, that was triggered by the queue an performed all of the recognition processing.
Lastly I still had some unreliability, this appears to have gone away by wrapping the request for a frame and conversion to an image in the same resource controling semaphore.

I am quite pleased with the performance now.  The camera is still cheap and needs good light, and I haven't got round to adding face recognition boxes to the imaage, but the project does work, and I feel that I have tamed the unreliability both from the brown outs and the potential memory problems in the two threads.

Have fun, and let me know if you try it and it works (or not).
