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



$headers = getallheaders();

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
    Header("X-Kannel-Plugin-Msg-Sms-Msgdata: I changed the message data");
    Header("X-Kannel-Plugin-Msg-Sms-Priority: 1");
    exit(0);
}
