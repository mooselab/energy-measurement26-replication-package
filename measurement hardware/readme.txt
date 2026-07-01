To build the experimental hardware, do the following: 

Bill of material: (Links provided are example only)
Soldering equipment (https://www.youtube.com/watch?v=Qps9woUGkvI)
Teensy 4.1 (https://www.pjrc.com/store/teensy41.html)
Ethernet kit for Teensy 4.1 (DigiKey Part Number1568-18615-ND)
INA226 breakout board (one per power rail)
USB-microB to USB type A cable (for teensy to computer connection)
1 1x2 Strip board 
30 cm female-female dupont jumper wire (5 per sensor breakout)
5x1 strip female Dupont jumper plastic sockets (or you can just buy dupont jumper with 5x1 headers but its more expensive that way)
2.54mm Female single Row pcb receptacle strip, with 24 sockets or more (for connecting teensy to the stripboard, recommended)
Wire Ferrule terminals and Crimper (for the cut ATX cables, recommended)
4-12 strips of 1mm 2.45mm pin headers (male) (ideally colored or labeled)

EPS-12v or ATX-12V 8 or 4 pin CPU power cable, male-female (if measuring CPU. Use compatible cables)
PCIE or compatible GPU power cable, male-female. (If measuring GPU. Use compatible cables)
ATX 24 pin male-female extender cable (if measuring motherboard)
(wouldn't recommend doing this with cables with metallic, nylon shields or sleeves, very messy)
Corsair RM850e modular power supply (if compatible, this is what I had)
Wire stripper 10-22 AWG (recommended) 
heat shrink tube (recommended)
hair dryer (for the heat shrink) 
Small screw driver 

Extra USB ethernet jacks if SUS or logging computer has less than 2 each (TP-Link USB to Ethernet Adapter (UE306)). 
Ethernet switch (TP-Link TL-SG105 5 Port Gigabit Unmanaged Ethernet Network Switch) (a wifi modem also works) 

Important: 
extra SMD shunt resistors (example: R002)

Assembly instruction: 

Step 1:
Depend on your goals, buy the appropriate cable for your hardware and consult the pin out diagram for the relevant standard. 
Alternatively, use a Digital multimeter (DMM) to measure the pin-out voltage of the pins. 
Use the TDP of the hardware to determine max expected power draw (without turbo). 
The max amp per board will be TDP / (number of 12v cables * 12 v)
For each power rail that delivers power (12v, 5v, 3v is typical), cut power rail in two and crimp the two ends. Heat shrink for durability.
Important: Don't cut the original power cables provided by the PSU, just in case the output is non-standard. Always get extra extension cables. Don't buy the shittiest ones on amazon.

Step 2: 
Assemble the teensy 4.1 and ethernet kit.
Assemble the INA226 sensor breakouts.
Plan out the busses on the stripboard (each bus need 5 strips)
Modify the strip board so that the VCC,GND for each sensor bus is connected to the relevant Teensy pins. 
Make sure the SCL, SDA bus line up with the corresponding SCL, SDA pins on the Teensy. See Teensy 4.1 pin out. 
Solder the two strips onto the strip board and mount the teensy 4.1. 
Solder the male header strips on to the bus. 

Step 3:
Calibration bits 
There is a large square SMD (surface mounted component) usually labeled 010 or R010 on the INA226 breakout boards, that is the shunt resistor. 
The Teensy 4.1 must send the calibration bits to the INA226 at power on for it to produce meaningful power measurements. 
See the attached spreadsheet: calibration formula for detail.
Since a INA226 has a upper voltage limit for measurement, and the max voltage that it is exposed to is in relation to the resistance of the shunt resistor,
by reducing the resistance of the shunt resistor, we can increase the power measurement ceiling of INA226. the trade-off is the step size. 
To measure high max currents, you can try soldering a R002 on top of the provided R010 so the two resistors are in parallel for a total resistance of 0.00167 Ohm. 
Depend on the calibration bits, the output bytes of the INA226 must be multiplied with a multiplier that is calculated with the calibration bits. 
Modify bus-configuration.h file according to your configuration.

Step 4: 
Bus configuration and INA226 addressees
Teensy 4.1 has 3 I2C busses.
If you'd like multiple sensor breakouts on the same bus, you have to modify their address by shorting designated address pins (A1, A0) with functional pins (see INA226 manual)
Typically, the breakout will provide you with 4x2 pairs of pads bridged by solder and this give you access to a total of 4 addresses. 
Additional addresses can be achieved by bridging the 5 pins with a jumper with the pads. Specifically which pad of the 4 pairs? Depends on the board.
For the ones I used, A0 and A1 is the inner 4 pads. 
short the appropriate pins so that no breakouts collide with their address on the same bus and modify bus-configuration.h according to your configuration. 

step 5: 
Connect the cut power supply cables to the sensor breakouts. Connect the PSU-side to VIN and Component-side to VOUT.
Connect Sensors to the bus with the jumpers. 
I would recommend labeling which side of the cable is VCC. If you plug it in backwards the sensor will break.
And label which bus the sensor belonged to.

Step 6: 
Upload the provided software to the teensy 4.1. I used arduino IDE. 
Connect the teensy 4.1 with the USB-typeB to the SYSTEM UNDER STUDY (important, or you may get garbage data)
Connect teensy 4.1 to ethernet switch
Connect SUS to the ethernet switch 
Connect Logging computer to ethernet switch
Download and compile the scripts: 
time provider -> SUS computer
rapl script -> SUS computer
workload script -> SUS computer
teensy software -> teensy 4.1
tcp logger -> logging computer 
Modify the ethernet connection setting to use the IPs hardcoded in the time provider script(SUS), teensy software(Teensy4.1), tcpLogger script(logging computer)
Alternatively modify the scripts themselves, the ip is hardcoded. It will work as long as they are on the same subnet. 

Step 7: 
Making a measurement: 
After restarting SUS, reconnect Teensy 4.1
run time provider script 
run RAPL polling script 
run tcp logger script 
run the workload
RAPL data will be saved in ./results and TcpLogger script will save data in specified path.



