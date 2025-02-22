Update 20250222: The V2 PCBs are now available for distribution. I have posted a few out already, in response to some advance orders, but I forgot to announce the availability in this github repo.  My apologies (again) for my unreliable memory. If you would like a V2 PCB (or even a V1, if you want to save some money), please send me an email: jim.sosnin@gmail.com. 

Update 20250112: Phil VK2ILO has received the V2 PCBs from the manufacturer. He is doing a test build to make sure everything works.  A preliminary version of the PCB manual (PDF) can be downloaded from this repo. It is complete except for the photos of a finished (all components mounted) V2 PCB.  The current photos included in it are of the V1 PCB.  Once Phil has finished his V2 test build, the manual will be updated with the new photos, and the V2 PCBs will be available for distribution.

Update 20241228: The V2 PCB, designed by Phil VK2ILO, should be available by mid-January 2025.
We have set a price of Au$10 plus postage for this V2 board.  In the meantime, a detailed description and assembly manual
for the board is nearly finished. It will be available via download from this git repo soon.

Earlier entries:

Notice: an alternative to the Arduino Nano may be needed for this project.
Most Arduino Nano modules available these days use a ceramic resonator to clock the processor, instead of a quartz crystal.
This causes too much drift in the tracking accuracy, although it does not affect frequency stability in 'hold' mode.
My prototype Track-hold-VFO project used a Nano module with a 16MHz crystal instead of a ceramic resonator.
See Photo 1 from the file list. Sorry about the oversight - I forgot to include that detail in the AR article.
I have only recently discovered that these crystal-equipped Nanos may no longer be available.

Alternatives:

1. A PCB has been developed by Philip Brown VK2ILO. See Photo 4. This is a 'thru-hole' design, not requiring any SMT work.
The design uses an Atmega 328P microprocessor, plus 16MHz crystal, in place of the Nano module. Programming is via a 6-pin
header for an FTDI 'breakout' interface. There is space on the PCB for the AD9850 DDS module. All the smaller components
for the complete Track-hold VFO fit on the PCB as well, apart from the status LEDs and control buttons. Testing has
revealed a couple of minor board faults in the initial run (V1), but these have easy workarounds and the V1 boards are
perfectly useable. More details will appear shortly, firstly in the upcoming (Nov-Dec 2024) AR magazine, and then
here, as a further update to this readme file. Also, a V2 PCB will be available soon, with the faults amended, plus some
other layout improvements. If you would like a V1 or a V2 PCB please send me an email: jim.sosnin@gmail.com.

2. (Earlier, now obsolete)
If you have an old Arduino Uno module that is not needed for anything else, a minor modification allows it to be used
in the original circuit in place of the Nano module. To modify the Uno, remove the 16MHz ceramic resonator clocking the
Atmega 328P, then add a 100 ohm resistor on the underside of the board, to drive the Atmega 328P from the Uno's other,
crystal-derived 16MHz clock, which drives the USB/serial interface. The resistor must be connected between the 'driven' end
of the 16MHz crystal and the 'X1' pin (pin 9) on the Atmega 328P. See Photos 2 and 3 from the file list.

To download the Arduino source code for this project:  
Right-click 'Tracking-VFO-VK3JST.ino' in the file list and select 'Save Link As...' from the drop-down menu.  
An alternative method is to left-click the file to display it on your screen, then click the 'Download' icon.
