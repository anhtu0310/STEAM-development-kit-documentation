# **Install CP2102 Driver on Windows System**

Download: [Windows driver](https://www.silabs.com/software-and-tools/usb-to-uart-bridge-vcp-drivers?tab=downloads)

<!-- Click the link to download ![image-20230426100746100](../img/image-20230426100746100.png), and we unzip it to ![image-20230426100852988](../img/image-20230426100852988.png). 

Unzip again and we will get the folder ![image-20230426101012182](../img/image-20230426101012182.png). Please remember the path of this folder for later use.  -->

 Right click Computer----- Properties----- Device Manager.  

![img](../img/wps5.jpg)

The yellow exclamation mark on the page implies an unsuccessful  installation and you should double click ![img](../img/wps6.jpg), then click “**Update Drive...**”to update the driver. 

![img](../img/wps7.jpg)

Click “**Browse my computer for drivers**” to find the downloaded Arduino software.

![img](../img/wps8.jpg)

There is a DRIVERS folder in Arduino software installed package, please open this folder and check the driver of CP210X series chips.

Click “Browse...”, then search the driver of CP2102 and click“Next”.

![wps9](../img/wps9.png)

After a while, the driver is installed successfully.

![img](../img/wps10.jpg) 

When opening the device manager, we will find that the yellow exclamation mark disappears, which means the driver of CP2102 is installed successfully.

![img](../img/wps11.jpg)