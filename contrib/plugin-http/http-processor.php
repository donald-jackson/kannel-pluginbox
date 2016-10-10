<?php
if (!function_exists('getallheaders'))
{
    function getallheaders()
    {
        $headers = '';
        foreach ($_SERVER as $name => $value)
        {
            if (substr($name, 0, 5) == 'HTTP_')
            {
                $headers[str_replace(' ', '-', ucwords(strtolower(str_replace('_', ' ', substr($name, 5)))))] = $value;
            }
        }
        return $headers;
    }
}

/** Full received header list is below (overwrite by returning same header key/values) **/

/**
  'X-Kannel-Plugin-Msg-Type' => 'sms',
  'X-Kannel-Plugin-Msg-Sms-Sender' => '33333',
  'X-Kannel-Plugin-Msg-Sms-Receiver' => '1234567891',
  'X-Kannel-Plugin-Msg-Sms-Udhdata' => '',
  'X-Kannel-Plugin-Msg-Sms-Msgdata' => 'Hello',
  'X-Kannel-Plugin-Msg-Sms-Time' => '1475831852',
  'X-Kannel-Plugin-Msg-Sms-Service' => 'tester',
  'X-Kannel-Plugin-Msg-Sms-Account' => '(null)',
  'X-Kannel-Plugin-Msg-Sms-Id' => '08f5cdaa-8cf7-4775-0100-10070000000b',
  'X-Kannel-Plugin-Msg-Sms-Mclass' => '-1',
  'X-Kannel-Plugin-Msg-Sms-Mwi' => '-1',
  'X-Kannel-Plugin-Msg-Sms-Coding' => '0',
  'X-Kannel-Plugin-Msg-Sms-Compress' => '-1',
  'X-Kannel-Plugin-Msg-Sms-Validity' => '-1',
  'X-Kannel-Plugin-Msg-Sms-Deferred' => '-1',
  'X-Kannel-Plugin-Msg-Sms-Pid' => '-1',
  'X-Kannel-Plugin-Msg-Sms-Rpi' => '-1',
  'X-Kannel-Plugin-Msg-Sms-Charset' => '(null)',
  'X-Kannel-Plugin-Msg-Sms-Binfo' => '(null)',
  'X-Kannel-Plugin-Msg-Sms-Priority' => '-1',

**/

$headers = getallheaders();

foreach($headers as $key => $value) {
    $headers[$key] = urldecode($value);
}

$sender = $headers['X-Kannel-Plugin-Msg-Sms-Sender'];
$receiver = $headers['X-Kannel-Plugin-Msg-Sms-Receiver'];

if($sender == '12345') {
    /* Decline this message */
    header("Status: 404 Not Found");
    exit(0);
}

if($sender == '11111') {
    /* Change the sender to 22222 */
    Header("X-Kannel-Plugin-Msg-Sms-Sender: 22222");
    exit(0);
}

if($receiver == '1234567890') {
    /* Changing the message content */
    Header("X-Kannel-Plugin-Msg-Sms-Msgdata: ".urlencode("I changed the message data"));
    Header("X-Kannel-Plugin-Msg-Sms-Priority: 1");
    exit(0);
}
