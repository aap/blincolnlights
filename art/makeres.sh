#!/bin/sh

files='key_n key_d key_u lamp_on lamp_off switch_d switch_u switch_r switch_l'
for i in $files; do
	xxd -i ${i}.png
done >panelart.inc
xxd -i pdp1_panel.png >pdp1art.inc
xxd -i ww_panel.png >wwart.inc
xxd -i b18_panel.png >b18art.inc
