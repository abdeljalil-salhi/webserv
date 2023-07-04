#include "../WebServer/WebServer.hpp"
#include "../Parsing/Request.hpp"


WebServ::WebServ(char **envp , std::vector<Server*> servers)
{
    this->servers = servers;
    this->envp = envp;
    parseMimeTypes();
    for(size_t i = 0; i < servers.size(); i++)
    {
        Socket socket(servers[i]->getListen(), servers[i]->getName());
        sockets.push_back(socket);
    }
	setupStatusCodes();
}

void WebServ::setupStatusCodes()
{
    std::ifstream file("status.codes");

    if (!file.is_open()) {
        std::cout << "Error opening file: " << "status.codes" << std::endl;
        return;
    }

    std::string line;
    while (std::getline(file, line))
    {
        int StatusCode;
        std::string error;
        std::istringstream ss(line);
        if (!(ss >> StatusCode))
        {
            std::cout << "Invalid StatusCode: " << line << std::endl;
            continue;
        }
        std::getline(ss, error);

        error = error.substr(error.find_first_not_of(' '));
        error = error.substr(0, error.find_last_not_of(' ') + 1);

        if (!error.empty())
            error_pages.insert(std::make_pair(StatusCode, error));
        else
            std::cout << "Missing error for StatusCode: " << StatusCode << std::endl;
    }
    file.close();
    maxFds = -1;
}


bool WebServ::eraseClient(ClientSocket client)
{
    close(client.getClientSocket());
    for(size_t i = 0; i < clients.size(); i++)
    {
        if(clients[i].getClientSocket() == client.getClientSocket())
        {
            clients.erase(clients.begin() + i);
            return true;
        }
    }
    exit(1);
}

bool WebServ::isAllowed(std::vector<std::string> methods, std::string method)
{
    for(size_t i = 0; i < methods.size(); i++)
        if(methods[i] == method)
            return true;
    return false;
}

void WebServ::sendToClient(ClientSocket &client, std::string msg)
{	
	int ret;
	if((ret = send(client.getClientSocket(), msg.c_str(), msg.size(), 0)) < 0)
        sendErrorToClient(500, client);
    else if(ret == 0)
        sendErrorToClient(400, client);
}

int WebServ::sendToClient(ClientSocket &client, char *msg, int size)
{	
	int ret;
	if((ret = send(client.getClientSocket(), msg, size, 0)) < 0)
        sendErrorToClient(500, client);
    else if(ret == 0)
        sendErrorToClient(400, client);
	return ret;
}

void WebServ::sendResponse(ClientSocket client, std::string dir, int code)
{	
    if(!dir.empty())
    {
        FILE *fd_s = fopen(dir.c_str(), "rb");
		if (fd_s == NULL)
			sendToClient(client ,  "HTTP/1.1 200\ret\nConnection: close\ret\nContent-Length: 120\ret\n\ret\n<!DOCTYPE html><head><title>makaynach</title></head><h1>wa hya location kayna walakin fin l file?</h1><body> </body></html>");
		else
		{
			fseek (fd_s , 0, SEEK_END);
			int lSize = ftell (fd_s);
			rewind (fd_s);
			fclose(fd_s);
			sendToClient(client,"HTTP/1.1 " + error_pages.find(code)->second + "\n" + "Content-Type: " + getMimeTypeFromExtension(dir) + "\nContent-Length: " + std::to_string(lSize) + "\n\n");
			int readFD = open(dir.c_str(), O_RDONLY);
			if (readFD < 0) return (sendErrorToClient(500, client));
			setToWait(readFD, readingSet);
			selection(readingSet, writingSet);
			char file[1024];
			int ret = read(readFD, file, 1024);
			if(ret < 0)
				sendErrorToClient(500, client);
			else 
			{
				while(ret)
				{
					if (sendToClient(client, file, ret) <= 0) break;
					setToWait(readFD, readingSet);
					selection(readingSet, writingSet);
					if((ret = read(readFD, file, ret)) < 0)
					{
						sendErrorToClient(500, client);
						break;
					}
					if(ret == 0)
						break;
				}
			}
			close(readFD);
		}
		return;
    }
	sendToClient(client ,  "HTTP/1.1 " + error_pages[code] + "\n\n");
}


std::string WebServ::fixUrl(std::string url, int serverID)
{
	std::string ret = servers[serverID]->getRoot();
    if(ret[ret.size() - 1] == '/')
        ret.erase(ret.size() - 1, 1);
    if(locations && !(locations->getRoot().empty()))
        url.erase(url.find(locations->getDir()), url.find(locations->getDir()) + locations->getDir().size());
    ret += url;
    return ret;
}

void WebServ::autoIndex(int socket, std::string path, std::string url, ClientSocket& client)
{
    struct dirent *ent;
    std::string tosend = "HTTP/1.1 200 OK\n\n<!DOCTYPE html>\n<html>\n<body>\n<h1>" + path + "</h1>\n<pre>\n";
	DIR *dir = opendir (url.c_str());
	if (!dir) {
        perror ("autoindex");
        return ;
    }
    while ((ent = readdir (dir)) != NULL)
        tosend += "<a href=\"" + ((std::string(ent->d_name) == ".") ? std::string(path) : (std::string(path) + "/" + std::string(ent->d_name))) + "\">" + std::string(ent->d_name) + "</a>\n";
    closedir (dir);
    tosend += "</pre>\n</body>\n</html>\n";
    int ret = send(socket , tosend.c_str(), tosend.size(), 0);
    if(ret < 0)
        sendErrorToClient(500, client);
    else if (ret == 0)
        sendErrorToClient(400, client);
}

Location *WebServ::getLocation(std::string url, int i)
{
    std::vector<Location *> locations = servers[i]->getLocation();
    for(size_t i = 0; i < locations.size(); i++)
    {
        if(strncmp(locations[i]->getDir().c_str(), url.c_str(), locations[i]->getDir().size()) == 0)
            return locations[i];
    }
    return NULL;
}

void WebServ::redirect(ClientSocket client, std::string url)
{
    std::string tosend = "HTTP/1.1 200 OK\n\n<head><meta http-equiv = \"refresh\" content = \"0; url =" + url + "\" /></head>";
    int ret = send(client.getClientSocket(), tosend.c_str(), tosend.size(), 0);
	if (ret > 0) return ;
	(ret < 0) ? sendErrorToClient(500, client) : sendErrorToClient(400, client);
}

void WebServ::sendErrorToClient(int err, ClientSocket &client)
{
    std::map<std::string , std::string> errpages = servers[client.getServerID()]->getError();
    if(errpages.find(std::to_string(err)) != errpages.end())
    {
        int fd = open(errpages[std::to_string(err)].c_str(), O_RDONLY);
        if(fd >= 0)
        {
            close(fd);
        	sendResponse(client, errpages[std::to_string(err)], 200);
        }
		close(fd);
    }
}

bool WebServ::selectAndWrite(std::string url, ClientSocket client, std::string str, Request req)
{
    int ret = 0;
    int fd = open(url.c_str(), O_WRONLY | O_TRUNC | O_CREAT, 0644);
    if(fd < 0)
    {
        sendErrorToClient(500, client);
        close(fd);
        return false;
    }
    setToWait(fd, writingSet);
    selection(readingSet, writingSet);
	(str == "") ? ret = write(fd, req.getBody().c_str(), req.getBody().size()) : ret = write(fd, str.c_str(), str.size());
    if(ret < 0)
    {
        sendErrorToClient(500, client);
        close(fd);
        return false;
    }
    close(fd);
    return true;
}

void WebServ::setToWait(int socket, fd_set &set)
{
    FD_SET(socket, &set);
    if(socket > maxFds)
        maxFds = socket;
}

void WebServ::selection(fd_set &read, fd_set &write)
{
    if((select(maxFds + 1, &read, &write, 0, 0)) < 0)
        exit(-1);
    writingSet = write;
    readingSet = read;
}

void WebServ::clientsQueue()
{
    fd_set read, write;
    FD_ZERO(&read);
    FD_ZERO(&write);
	size_t i = 0;
	while (i < sockets.size())
	{
		setToWait(sockets[i].getSocket(), read);
		if (i < clients.size())
			setToWait(clients[i].getClientSocket(), read);
		i++;
	}
    selection(read, write);
}

void WebServ::createClients()
{
    sockaddr_in addrclient;
    socklen_t clientSize = sizeof(addrclient);
	size_t i = 0;

	while (i < sockets.size())
	{
		if(FD_ISSET(sockets[i].getSocket(), &readingSet))
        {
			int addClient = accept(sockets[i].getSocket(), (struct sockaddr *)&addrclient, &clientSize);
            if(addClient < 0)
				pexit("accept: ");
			ClientSocket client(addClient, i);
			clients.push_back(client);
        }
		i++;
	}
}

void WebServ::clientError(int err, ClientSocket &client)
{
	sendErrorToClient(err, client);
    eraseClient(client);
}

int WebServ::checkRequest(Request &request, ClientSocket &client, int size)
{
	if (size < 0)
    {
        clientError(500, client);
        return 1;
    }
    else if(SIZE_MAX == 0)
	{
        eraseClient(client);
        return 1;
    }
	if ((request.getMethod() != "POST" && request.getMethod() != "GET" && request.getMethod() != "DELETE"))
    {
        clientError(405, client);
        return 1;       
    }
	if (request.getProtocol() != "HTTP/1.1")
	{
		clientError(505, client);
        return 1;    
	}
    if(request.getContentLength() != std::string::npos && request.getContentLength() > (size_t)stoi(servers[client.getServerID()]->getBody()))
    {
        clientError(413, client);
        return 1;
    } 
	// need to check unautorised methods
	return 0;
}

void WebServ::runMethods(ClientSocket &client, std::string url, Request &request)
{
	locations = getLocation(url, client.getServerID());
	if(locations && !locations->getRedir().empty())
        redirect(client, locations->getRedir());
    else if (request.getMethod() == "GET")
        GET(client, url);
    else if (request.getMethod() == "DELETE")
        DELETE(client, url);
}

void WebServ::manageClients()
{
    for(size_t i = 0; i < clients.size(); i++)
    {
        if(FD_ISSET(clients[i].getClientSocket(), &readingSet))
        {
            size_t Reqsize = recv(clients[i].getClientSocket(), clients[i].buffer, 65536, 0);
            clients[i].size = Reqsize;
			std::string str(clients[i].buffer);
			clients[i].request = str.substr(0, Reqsize);
            Request request((char *)clients[i].request.c_str());
            request.print_req();
			if (checkRequest(request, clients[i], Reqsize))
				continue;
            std::string url = request.getPath();
            // check if url contains cgi-bin
            //check if cgi allowed in location
            // exec cgi
			runMethods(clients[i], url, request);
            eraseClient(clients[i]);
        }
    }
}

void WebServ::GET(ClientSocket &client, std::string url)
{
	struct stat s;
    std::string toSend = fixUrl(url, client.getServerID());
    if (url.size() >= 64)return(sendErrorToClient(414, client));
    if (locations && !(locations->getIndex().empty() && url ==  locations->getDir()))
        return (sendResponse(client, locations->getRoot() + locations->getIndex(), 200));
	
		std::ifstream fd( toSend.c_str(), std::fstream::binary );
		stat(toSend.c_str(), &s);
		if(!fd.is_open())
			sendErrorToClient(404, client);
		else
		{
			if(S_ISDIR(s.st_mode))
			{
				if(url == "/")
					sendResponse(client, toSend + servers[client.getServerID()]->getIndex(), 200);
				else if(servers[client.getServerID()]->getautoindex() == "on" || (locations && locations->getautoindex() == "on"))
					autoIndex(client.getClientSocket(), url, toSend, client);
				else
					sendErrorToClient(404, client);
			}
			else
				sendResponse(client, toSend, 200);
		}
		fd.close(); 
}


void removeFile(std::string str)
{
    struct dirent *direct;
    struct stat s;
    stat(str.c_str(), &s);
    str += "/";
    DIR *dir = opendir(str.c_str());
    while (S_ISDIR(s.st_mode) && (direct = readdir(dir)) != NULL)
    {
        std::string dName(direct->d_name);
        std::string str1 = str + direct->d_name;
        struct stat s1;
        stat(str1.c_str(), &s1);
        if (dName == "." || dName == "..")
            return ;
        if (S_ISDIR(s1.st_mode))
            removeFile(str + direct->d_name);
        else if (!S_ISDIR(s1.st_mode))
             unlink(((const char *)str1.c_str()));
    }
    if (dir)
        closedir(dir);
    remove(str.c_str());
}


std::string findPWD(char **envp)
{
    const char* value = nullptr;
    const char* key = "PWD=";

    
    for (int i = 0; envp[i] != nullptr; ++i) {
        if (strncmp(envp[i], key, strlen(key)) == 0) {
            value = envp[i] + strlen(key);
            break;
        }
    }
    std::string ret(value);
    return ret;
}


void WebServ::DELETE(ClientSocket &client, std::string url)
{
    std::cout << url << std::endl;
    // need to add root
    std::string path = findPWD(envp) + url;
	struct stat s;
	std::cout << path << std::endl;
    stat(path.c_str(), &s);
    if (!S_ISDIR(s.st_mode))
        unlink(((const char *)path.c_str()));
    else
        removeFile(path);
    std::string response = (char *)"HTTP/1.1 200\ret\nConnection: close\ret\nContent-Length: 70";
    response += "\ret\n\ret\n<!DOCTYPE html><head><title>200 OK</title></head><body> </body></html>";
    send(client.getClientSocket(), response.c_str(), response.size(), 0);
}

void WebServ::POST()
{
	
}

void pexit(std::string err)
{
	perror(err.c_str());
	exit(1);
}

void WebServ::parseMimeTypes(void)
{
	std::ifstream file("mime.types");

	if (file.is_open())
	{
		std::string line;
		while (std::getline(file, line))
		{
			line = trimString(line);
			if (line.empty())
				continue;

			std::istringstream iss(line);
			std::string mimeType;
			iss >> mimeType;
			if (mimeType == "types" || mimeType == "}")
				continue;

			std::string extension;
			while (iss >> extension)
			{
				if (extension[extension.size() - 1] == ';')
					extension = extension.substr(0, extension.size() - 1);
				this->_mimeTypes[extension] = mimeType;
			}
		}
		file.close();
	}
	else
	{
		std::cerr << "Error: mime.types.conf file not found" << std::endl;
		exit(1);
	}
}

void WebServ::printMimeTypes(void)
{
	for (std::map<std::string, std::string>::iterator it = this->_mimeTypes.begin(); it != this->_mimeTypes.end(); it++)
		std::cout << it->first << " " << it->second << std::endl;
}

std::string WebServ::getMimeType(std::string &extension)
{
	try
	{
		return this->_mimeTypes.at(extension);
	}
	catch (const std::exception &e)
	{
		return "text/plain";
	}
}

std::string WebServ::getExtensionFromUrl(const std::string& url)
{
    std::size_t lastDotPos = url.find_last_of('.');
    if (lastDotPos == std::string::npos || lastDotPos == url.length() - 1)
        return ""; 
    else
        return url.substr(lastDotPos + 1);
}

std::string WebServ::getMimeTypeFromExtension(const std::string& url)
{
    std::string extension = getExtensionFromUrl(url);
    if (!extension.empty())
	{
        std::map<std::string, std::string>::const_iterator it = _mimeTypes.find(extension);
        if (it != _mimeTypes.end())
		{
            return it->second;
        }
    }
    return "";
}

std::string trimString(const std::string& str) {
    std::string trimmed = str;
    std::string::size_type pos = 0;
    while (pos < trimmed.length() && (trimmed[pos] == ' ' ||  trimmed[pos] == '\t')) {
        ++pos;
    }
    trimmed.erase(0, pos);
    pos = trimmed.length() - 1;
    while (pos > 0 && (trimmed[pos] == ' ' || trimmed[pos] == '\t')) {
        --pos;
    }
    trimmed.erase(pos + 1);
    return trimmed;
}

void WebServ::run()
{
    while(1337)
    {
        clientsQueue();
        createClients();
        manageClients();
    }
}