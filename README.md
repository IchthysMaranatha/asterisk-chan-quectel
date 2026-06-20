Channel driver for Quectel and Simcom modules 
=================================================
## Notes

This fork includes compatibility fixes for Quectel EC20 modules, including improved call state handling and QTONEDET based DTMF support for IVR applications.

This should work with Quectel modules such as EC20, EC21, EC25, EG9x and Simcom sim7600 and possibly other models with voice over USB capability. Tested with the EC25-E mini-pcie module and Waveshare sim7600 g-h dongle. If the product page of your Quectel module contains the application note Voice over USB and UAC or Voice over UAC, you should be good to go. Praise God! Have been able to integrate ALSA support for UAC mode, steps to use UAC can be found <a href="https://github.com/IchthysMaranatha/asterisk-chan-quectel/discussions/2">here</a> If using Quectel serial audio port, please ensure gps messages has been turned off with AT+QGPSCFG="outport","none"

Note for Pi users: If using a Pi and especially a device older than Pi4, you will invariably have problems of various kinds if powering the modem using a Pi usb port due to the current limiting circuit (600ma - 1.2A). Even with a Pi 4, you will not be able to stably handle modem devices with a larger power draw (2.1A) than that of the waveshare sim7600 dongle. You will need to provide independent power to the modem without using the Pi usb port which must only be used for data communication. If the modem device is a Pi HAT, you can provide power by stacking and using a beefy power supply. 

I'm not a C programmer at all and this write-up may help those interested in improving this driver and also for supporting other modules such as those from Telit, U-blox, Sierra Wireless etc. 

A key part is understanding call handling by the driver. Everything revolves around URCs (Unsolicted Result Code) received at the modem terminal when certain events occur. So for Huawei when dialling out an ^ORIG URC is generated, when an outgoing or incoming call is connected there appears a ^CONN URC and when a call is terminated by either party a ^CEND URC is received. The RING URC for incoming calls is universal but all the others mentioned are specific to Huawei and appropriate changes are to be made in <a href="https://github.com/IchthysMaranatha/asterisk-chan-quectel/blob/master/at_response.h">at_response.h</a> and <a href="https://github.com/IchthysMaranatha/asterisk-chan-quectel/blob/master/at_response.c">at_response.c</a> to handle calls correctly. 

In many modules, when AT^DSCI=1 is set, URC for call status indication is generated. For example the URC ^DSCI: 4,1,3,0,+XXXXXXXXX,145 indicates that an incoming voice call has been connected. The URC ^DSCI: 3,0,6,0,XXXXXXX,129 indicates an outgoing voice call has terminated. ^DSCI can be activated on modules by many manufacturers such as Quectel, Simcom, Telit, Sierra Wireless etc. There may be some differences, for example my Simcom 7600 module does not generate the call termination URC.

Armed with this knowledge and using AT^DSCI=1 as an intialization command, we can now set up the Huawei ^ORIG URC <a href="https://github.com/IchthysMaranatha/asterisk-chan-quectel/blob/b6f6a389f2d8cc0b1ba183f6ebe7f140e03730af/at_response.h#L32">here</a> to handle entire call management (initiatiing, connecting in or out, terminating) through code block <a href="https://github.com/IchthysMaranatha/asterisk-chan-quectel/blob/b6f6a389f2d8cc0b1ba183f6ebe7f140e03730af/at_response.c#L612-L737">here</a> with necessary changes.


## Quectel EC20

Tested with:

- Quectel EC20
- Asterisk 22
- FreePBX 17
- USB Audio Class (UAC)

Additional changes:

- QTONEDET DTMF event support
- Improved call state processing
- Better IVR compatibility

Building:
----------
    $ apt install -y build-essential autoconf automake libtool pkg-config asterisk22-devel libasound2-dev libsqlite3-dev
    $ ./bootstrap
    $ ./configure --with-astversion=22.8.0 DESTDIR=/usr/lib/x86_64-linux-gnu/asterisk/modules
    $ make
    $ make install
    copy quectel.conf to /etc/asterisk Change context and audio serial port as required

If you run a different version of Asterisk, you'll need to update the
`22.8` as appropriate, obviously.

If you did not `make install` Asterisk in the usual location and configure
cannot find the asterisk header files in `/usr/include/asterisk`, you may
optionally pass `--with-asterisk=PATH/TO/INCLUDE`.

Here is an example for the dialplan:
------------------------------------

**WARNING**: *This example uses the raw SMS message passed to System() directly.
No sane person would do that with untrusted data without escaping/removing the
single quotes.*

    [quectel-incoming]
    exten => sms,1,Verbose(Incoming SMS from ${CALLERID(num)} ${BASE64_DECODE(${SMS_BASE64})})
    exten => sms,n,System(echo '${STRFTIME(${EPOCH},,%Y-%m-%d %H:%M:%S)} - ${QUECTELNAME} - ${CALLERID(num)}: ${BASE64_DECODE(${SMS_BASE64})}' >> /var/log/asterisk/sms.txt)
    exten => sms,n,Hangup()

    exten => ussd,1,Verbose(Incoming USSD: ${BASE64_DECODE(${USSD_BASE64})})
    exten => ussd,n,System(echo '${STRFTIME(${EPOCH},,%Y-%m-%d %H:%M:%S)} - ${QUECTELNAME}: ${BASE64_DECODE(${USSD_BASE64})}' >> /var/log/asterisk/ussd.txt)
    exten => ussd,n,Hangup()

    exten => s,1,Dial(SIP/2001@othersipserver)
    exten => s,n,Hangup()

    [othersipserver-incoming]

    exten => _X.,1,Dial(Quectel/r1/${EXTEN})
    exten => _X.,n,Hangup

    exten => *#123#,1,QuectelSendUSSD(quectel0,${EXTEN})
    exten => *#123#,n,Answer()
    exten => *#123#,n,Wait(2)
    exten => *#123#,n,Playback(vm-goodbye)
    exten => *#123#,n,Hangup()

    exten => _#X.,1,QuectelSendSMS(quectel0,${EXTEN:1},"Please call me",1440,yes,"magicID")
    exten => _#X.,n,Answer()
    exten => _#X.,n,Wait(2)
    exten => _#X.,n,Playback(vm-goodbye)
    exten => _#X.,n,Hangup()

You can also use this:
----------------------

Call using a specific group:

    exten => _X.,1,Dial(Quectel/g1/${EXTEN})

Call using a specific group in round robin:

    exten => _X.,1,Dial(Quectel/r1/${EXTEN})

Call using a specific quectel:

    exten => _X.,1,Dial(Quectel/quectel0/${EXTEN})

Call using a specific provider name:

    exten => _X.,1,Dial(Quectel/p:PROVIDER NAME/${EXTEN})

Call using a specific IMEI:

    exten => _X.,1,Dial(Quectel/i:123456789012345/${EXTEN})

Call using a specific IMSI prefix:

    exten => _X.,1,Dial(Quectel/s:25099203948/${EXTEN})

How to store your own number:

    quectel cmd quectel0 AT+CPBS=\"ON\"
    quectel cmd quectel0 AT+CPBW=1,\"+123456789\",145


Other CLI commands:
-------------------

    quectel reset <device>
    quectel restart gracefully <device>
    quectel restart now <device>
    quectel restart when convenient <device>
    quectel show device <device>
    quectel show devices
    quectel show version
    quectel sms <device> number message
    quectel ussd <device> ussd
    quectel stop gracefully <device>
    quectel stop now <device>
    quectel stop when convenient <device>
    quectel start <device>
    quectel restart gracefully <device>
    quectel restart now <device>
    quectel restart when convenient <device>
    quectel remove gracefully <device>
    quectel remove now <device>
    quectel remove when convenient <device>
    quectel reload gracefully
    quectel reload now
    quectel reload when convenient


