#!/usr/bin/dotnet script
using System;
using System.IO;
using System.Text;
using System.Runtime.CompilerServices; // Required for CallerFilePath

// 1. This helper uses the C# compiler to grab the exact path of THIS script file
string GetScriptDirectory([CallerFilePath] string path = "") 
{
    return Path.GetDirectoryName(path) ?? "";
}

// 2. Dynamically build the path based on where the script lives
string baseDir = GetScriptDirectory();
string filePath = Path.Combine(baseDir, "Hello.cs.htm");

if (File.Exists(filePath)) 
{
    string content = File.ReadAllText(filePath);
    int bodySize = content.Length;
    string date = DateTime.UtcNow.ToString("r"); 

    string header = 
        $"Status: 200 OK\r\n" +
        $"Date: {date}\r\n" +
        $"Server: Enginx/CGI-CSharp\r\n" +
        $"Content-Type: text/html; charset=utf-8\r\n" +
        $"Content-Length: {bodySize}\r\n" +
        // $"Connection: close\r\n" +
        $"\r\n"; 

    Console.Write(header);
    Console.Write(content);
}
else 
{
    // Print exactly where it looked so you can debug if it fails again
    string errorContent = $"<html><body><h2>404 - File not found</h2><p>Looked in: {filePath}</p></body></html>";
    int bodySize = Encoding.UTF8.GetByteCount(errorContent);
    string date = DateTime.UtcNow.ToString("r");

    string errorHeader = 
        $"Status: 404 Not-Found\r\n" +
        $"Date: {date}\r\n" +
        $"Server: Enginx/CGI-CSharp\r\n" +
        $"Content-Type: text/html; charset=utf-8\r\n" +
        $"Content-Length: {bodySize}\r\n" +
        // $"Connection: close\r\n" +
        $"\r\n";

    Console.Write(errorHeader);
    Console.Write(errorContent);
}