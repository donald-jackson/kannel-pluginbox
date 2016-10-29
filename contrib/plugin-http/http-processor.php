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
  'x-kannel-plugin-msg-type' => 'sms',
  'x-kannel-plugin-msg-sms-sender' => '33333',
  'x-kannel-plugin-msg-sms-receiver' => '1234567891',
  'x-kannel-plugin-msg-sms-udhdata' => '',
  'x-kannel-plugin-msg-sms-msgdata' => 'hello',
  'x-kannel-plugin-msg-sms-time' => '1475831852',
  'x-kannel-plugin-msg-sms-service' => 'tester',
  'x-kannel-plugin-msg-sms-account' => '(null)',
  'x-kannel-plugin-msg-sms-id' => '08f5cdaa-8cf7-4775-0100-10070000000b',
  'x-kannel-plugin-msg-sms-mclass' => '-1',
  'x-kannel-plugin-msg-sms-mwi' => '-1',
  'x-kannel-plugin-msg-sms-coding' => '0',
  'x-kannel-plugin-msg-sms-compress' => '-1',
  'x-kannel-plugin-msg-sms-validity' => '-1',
  'x-kannel-plugin-msg-sms-deferred' => '-1',
  'x-kannel-plugin-msg-sms-pid' => '-1',
  'x-kannel-plugin-msg-sms-rpi' => '-1',
  'x-kannel-plugin-msg-sms-charset' => '(null)',
  'x-kannel-plugin-msg-sms-binfo' => '(null)',
  'x-kannel-plugin-msg-sms-priority' => '-1',

**/

$original_headers = getallheaders();

$headers = array();
foreach($original_headers as $key => $value) {
  $key = strtolower($key);
  $headers[$key] = urldecode($value);
}

$sender = $headers['x-kannel-plugin-msg-sms-sender'];
$receiver = $headers['x-kannel-plugin-msg-sms-receiver'];

if($sender == '12345') {
    /* Decline this message */
    header("HTTP/1.0 404 Not Found");
    exit(0);
}

if($sender == '11111') {
    /* Change the sender to 22222 */
    header("x-kannel-plugin-msg-sms-sender: 22222");
    exit(0);
}

if($receiver == '1234567890') {
    /* Changing the message content */
    header("x-kannel-plugin-msg-sms-msgdata: ".urlencode("I changed the message data"));
    header("x-kannel-plugin-msg-sms-priority: 1");
    exit(0);
}
