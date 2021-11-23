Channel driver for Quectel and Simcom modules 
=================================================
This should work with Quectel modules such as EC20, EC21, EC25, EG9x and Simcom sim7600 and possibly other models with voice over USB capability. Tested with the EC25-E mini-pcie module and Waveshare sim7600 g-h dongle. If the product page of your Quectel module contains the application note Voice over USB and UAC, you should be good to go. If it only has the note Voice over UAC, you can still use this driver but you will need to use an additional Asterisk alsa channel with it and bridge that with every call. Steps to use it can be found <a href="https://github.com/IchthysMaranatha/asterisk-chan-quectel/discussions/2">here</a>

I'm not a C programmer at all and this write-up may help those interested in improving this driver and also for supporting other modules such as those from Telit, U-blox, Sierra Wireless etc. 

A key part is understanding call handling by the driver. Everything revolves around URCs (Unsolicted Result Code) received at the modem terminal when certain events occur. So for Huawei when dialling out an ^ORIG URC is generated, when an outgoing or incoming call is connected there appears a ^CONN URC and when a call is terminated by either party a ^CEND URC is received. The RING URC for incoming calls is universal but all the others mentioned are specific to Huawei and appropriate changes are to be made in <a href="https://github.com/IchthysMaranatha/asterisk-chan-quectel/blob/master/at_response.h">at_response.h</a> and <a href="https://github.com/IchthysMaranatha/asterisk-chan-quectel/blob/master/at_response.c">at_response.c</a> to handle calls correctly. 

In many modules, when AT^DSCI=1 is set, URC for call status indication is generated. For example the URC ^DSCI: 4,1,3,0,+XXXXXXXXX,145 indicates that an incoming voice call has been connected. The URC ^DSCI: 3,0,6,0,XXXXXXX,129 indicates an outgoing voice call has terminated. ^DSCI can be activated on modules by many manufacturers such as Quectel, Simcom, Telit, Sierra Wireless etc. There may be some differences, for example my Simcom 7600 module does not generate the call termination URC.

Armed with this knowledge and using AT^DSCI=1 as an intialization command, we can now set up the Huawei ^ORIG URC <a href="https://github.com/IchthysMaranatha/asterisk-chan-quectel/blob/b6f6a389f2d8cc0b1ba183f6ebe7f140e03730af/at_response.h#L32">here</a> to handle entire call management (initiatiing, connecting in or out, terminating) through code block <a href="https://github.com/IchthysMaranatha/asterisk-chan-quectel/blob/b6f6a389f2d8cc0b1ba183f6ebe7f140e03730af/at_response.c#L612-L737">here</a> with necessary changes.

Once this is correctly done, we need to make sure that USB voice is properly activated for the call. For the Quectel module AT+QPCMV=1,0 enables voice over the USB emulated serial port. Unlike Huawei, this needs to be done before dialling out or receivng the call. We can also set it up as an initialization command, but problem with this is that after a few calls voice may stop working and again a reset with AT+QPCMV=0 followed by AT+QPCMV=1,0 is needed. So I've appended AT+QPCMV=0 and AT+QPCMV=1,0 with every ATD and ATA command as done <a href="https://github.com/IchthysMaranatha/asterisk-chan-quectel/blob/b6f6a389f2d8cc0b1ba183f6ebe7f140e03730af/at_command.c#L526">here</a> and <a href="https://github.com/IchthysMaranatha/asterisk-chan-quectel/blob/b6f6a389f2d8cc0b1ba183f6ebe7f140e03730af/at_command.c#L571">here</a> Voice can also be set up over virtual USB sound card with AT+QPCMV=1,2 but I'll touch upon that later.

For Simcom voice activation over USB with AT+CPCMREG=1 can be done only after a call is connected. It will persist even after a call but with the same issue that after a few calls it will need a reset with AT+CPCMREG=0. So one can use CPCMREG=0 before every DIAL or ANSWER and CPCMREG=1 after call connection. The CPCMREG=1 cannot imeediately follow ATD OR ATA as it can throw an error or freeze up (I do not know if this is a problem with just my module). Simcom was very tricky to get working as there is no proper call termination URC and its serial audio port also does not play well with the chan_dongle code. But thanks to God, was finally able to get it working. Still remains a hack job as call ids cannot be matched for termination. If your call ids are different from 3 or 4 for outgoing and incoming calls, you'll need to make changes in this block <a href="https://github.com/IchthysMaranatha/asterisk-chan-quectel/blob/08d3bcad0de21f93eaad9652ccb48ecb9b04a8e6/at_response.c#L105-L143">here</a>

Way forward: This <a href="http://laforge.gnumonks.org/blog/20170902-cellular_modems-voice/">justified rant</a> by a professional in the industry points out that the virtual sound card solution is the way voice should be provided. We can already see that in few modules from Quectel, Telit, u-blox, Sierra Wireless etc. In such a scenario, development needs to be done for an integrated alsa voice channel for this driver. One can also use this driver as a control channel and chan_console or chan_alsa as the voice channel. So for EC25, one could set up AT+QPCMV=1,2 and then use bridged alsa channel which points to the modem sound card for every incoming and outgoing call. For EC25, one time USB configuration enabling UAC also needs to done as per doc <a href="https://forums.quectel.com/uploads/short-url/xnztA07u1c4hilREu66LYIY6NGR.pdf">here</a>

Those with problems with chan_mobile with certain bluetooth controllers such as the in built Raspberry Pi one may find a solution <a href="https://blog.maplein.com/2021/09/fix-for-audio-issues-with-asterisk-and.html">here</a>

Thanks be to the Father of Lights from Whom are all good things.

In these days of great darkness, when truth and justice are outlawed and where believers themselves are greater obstacles than the atheists with their bigotry, hatred, self-righteousness, falsehood and pride you may just find God speaking to you through this random message to Vassula https://ww3.tlig.org/en/the-messages/messages-random/ or a random verse from the bible www.sandersweb.net/bible/verse.php

I do not need donations, but if you wish you could donate to the person I've forked this from. Or if so inclined, you could donate here https://bethmyriam.org/ 

Building:
----------

    $ ./bootstrap
    $ ./configure --with-astversion=16.20
    $ make
    $ make install
    copy quectel.conf to /etc/asterisk Change context and audio serial port as required

If you run a different version of Asterisk, you'll need to update the
`16.20` as appropriate, obviously.

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


