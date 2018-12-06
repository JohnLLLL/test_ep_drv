#!/bin/sh

TOOL_PATH=../app
DRIVER_PATH=../src

function init_chan()
{
	$TOOL_PATH/cfg_chan $1 $2 0 1
}

function free_chan()
{
	$TOOL_PATH/cfg_chan $1 $2 0 0
}

function pause_chan()
{
	$TOOL_PATH/cfg_chan $1 $2 1 $3
}

function halt_chan()
{
	$TOOL_PATH/cfg_chan $1 $2 2 $3
}

function reset_chan()
{
	$TOOL_PATH/cfg_chan $1 $2 3 $3
}

function reset_drv()
{
	rmmod dma_drv
	insmod $DRIVER_PATH/dma_drv.ko
}

function send_cmd()
{
	$TOOL_PATH/send_cmd $1 $2 $3 $4
}

# initialize the driver 
insmod $DRIVER_PATH/dma_drv.ko

