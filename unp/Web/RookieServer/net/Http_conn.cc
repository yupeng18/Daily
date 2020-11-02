#include "Http_conn.h"
#include <wait.h>

void Http_conn::initmsg()
{
	path = "";
	filetype = "";
	keepalive = false;
}

Http_conn::Http_conn(SP_Channel Channel)
:	channel(Channel),
	storage(getconf().getstorage()),
	parsestate(PARSE_METHOD),
	pos(0),
	inbuffer(""),
	path(""),
	filetype(""),
	keepalive(false),
	size(0)
{
	handleparse[0] = bind(&Http_conn::parseError, this);
	handleparse[1] = bind(&Http_conn::parseMethod, this);
	handleparse[2] = bind(&Http_conn::parseHeader, this);
	handleparse[3] = bind(&Http_conn::parseSuccess, this);
	channel->setReadhandler(bind(&Http_conn::parse, this));
}

Http_conn::~Http_conn()
{	
}

//从inbuffer中找到从pos开始的第一个c字符
bool Http_conn::Read(string& msg, string str)
{
	int next = inbuffer.find(str, pos);
	if(string::npos == next)
		return false;
	msg = inbuffer.substr(pos, next - pos);
	pos = next + str.length();
	return true;
}

PARSESTATE Http_conn::parseMethod()
{
	string msg;
	if(!Read(msg, " /"))
		return PARSE_ERROR;
	else if("GET" == msg)
		method = METHOD_GET;
	else if("POST" == msg)
		method = METHOD_POST;
	else
		return PARSE_ERROR;
	if(!Read(path, " "))
		return PARSE_ERROR;
	struct stat sbuf;
	if("" == path)
		path = "index.html";
    if(stat((storage + path).c_str(), &sbuf) < 0)
    {
		LOG << "no file";
        parsestate = PARSE_ERROR;
        return PARSE_ERROR;
    }
	size = sbuf.st_size;
	if(!Read(msg, "\r\n"))
		return PARSE_ERROR;
	else if("HTTP/1.1" == msg)
		version = HTTP_11;
	else
		return PARSE_ERROR;
	return PARSE_HEADER;
}

PARSESTATE Http_conn::parseHeader()
{
	if(inbuffer[pos] == '\r' && inbuffer[pos + 1] == '\n')
		return PARSE_SUCCESS;
	string key, value;
	if(!Read(key, ": "))
		return PARSE_ERROR;
	if(!Read(value, "\r\n"))
		return PARSE_ERROR;
	header[key] = value;
	return PARSE_HEADER;
}

PARSESTATE Http_conn::parseError()
{
	LOG << "parse error";
	inbuffer = "";
	pos = 0;
	initmsg();
	header.clear();
	channel->setRevents(EPOLLOUT | EPOLLET);
	channel->getLoop().lock()->updatePoller(channel);
	channel->setWritehandler(bind(&Http_conn::handleError, this, 400, "Bad Request"));
	return PARSE_METHOD;
}

PARSESTATE Http_conn::parseSuccess()
{
	channel->setRevents(EPOLLOUT | EPOLLET);
	channel->getLoop().lock()->updatePoller(channel);
	inbuffer = inbuffer.substr(pos + 2);
	pos = 0;
	if(HTTP_11 == version && header.find("Connection") != header.end() && ("Keep-Alive" == header["Connection"] || "keep-alive" == header["Connection"]))
		keepalive = true;
	int dot_pos = path.find('.');
	if(string::npos == dot_pos)
		filetype = ContentType::getCont("default");
	else
		filetype = ContentType::getCont(path.substr(dot_pos));
	channel->setWritehandler(bind(&Http_conn::send, this));
	header.clear();
	return PARSE_METHOD;
}

void Http_conn::parse()
{
	bool zero = false;
	int readsum;
    readsum = readn(channel->getFd(), inbuffer, zero);
    if(readsum < 0 || zero)
    {
        initmsg();
        channel->setDeleted(true);
        channel->getLoop().lock()->addTimer(channel, 0);
		return;
        //读到RST和FIN默认FIN处理方式
    }
    cout << inbuffer << endl;
	while(inbuffer.length() && ~inbuffer.find("\r\n", pos))
		parsestate = handleparse[parsestate]();
}

void Http_conn::send()
{
	string outbuffer = "";
	if(METHOD_POST == method)
    {
        const char *path_ = "/home/liyupeng/Daily/unp/Web/RookieServer/test/cgi/add";
        const char *arg_ = "add";
        const char *buffer = inbuffer.c_str();
        char argv[100];
        int a, b;
        sscanf(buffer, "a=%d&b=%d", &a, &b);
        sprintf(argv, "%d&%d", a, b);
        if(fork() == 0)
        {
            dup2(channel->getFd(), STDOUT_FILENO);
            execl(path_, arg_, argv, NULL);
        }
        wait(NULL);

	    initmsg();
	    channel->setRevents(EPOLLIN | EPOLLET);
	    channel->getLoop().lock()->updatePoller(channel);
	}
	else if(METHOD_GET == method)
    {
		if(keepalive)
        {
			outbuffer = "HTTP/1.1 200 OK\r\n";
			outbuffer += string("Connection: Keep-Alive\r\n") + "Keep-Alive: timeout = " + to_string(getconf().getkeepalive()) + "\r\n";
			channel->getLoop().lock()->addTimer(channel,getconf().getkeepalive());
		}
		outbuffer += "Content-Type: " + filetype + "\r\n";
		outbuffer += "Content-Length: " + to_string(size) + "\r\n";
        outbuffer += "Server: Rookie Web Server\r\n";
		outbuffer += "\r\n";
		if(!(getCache().get(path, outbuffer)))
        {
			int src_fd = Open((storage + path).c_str(), O_RDONLY, 0);
			char *src_addr = (char *)mmap(NULL, size, PROT_READ, MAP_PRIVATE, src_fd, 0);
			Close(src_fd);
			string context(src_addr, size);
			outbuffer += context;
			getCache().set(path, context);
			munmap(src_addr, size);
		}
	}
	const char *buffer = outbuffer.c_str();
	if(!writen(channel->getFd(), buffer, outbuffer.length()))
		LOG << "writen error";
	initmsg();
	channel->setRevents(EPOLLIN | EPOLLET);
	channel->getLoop().lock()->updatePoller(channel);
}

void Http_conn::handleError(int errornum, string msg)
{   
    //暂时统一用400,Bad Request来处理,留接口可在上层修改错误码和错误信息
	string body = "<html><title>哎~出错了</title>";
	body += "<head><meta http-equiv=\"Content-Type\" content=\"text/html; charset=utf-8\"></head>";
	body += "<body bgcolor=\"ffffff\">";
	body += to_string(errornum) + msg;
    body += "<hr><em> Rookie Web Server</em>\n</body></html>";
	string outbuffer = "HTTP/1.1 " + to_string(errornum) + msg + "\r\n";
    outbuffer += "Content-Type: text/html\r\n";
    outbuffer += "Connection: Close\r\n";
    outbuffer += "Content-Length: " + to_string(body.size()) + "\r\n";
    outbuffer += "Server: Rookie Web Server\r\n";;
    outbuffer += "\r\n";	
	outbuffer +=body;
    const char *buffer = outbuffer.c_str();
    if(!writen(channel->getFd(), buffer, outbuffer.length()))
    	LOG << "writen error";
    channel->setRevents(EPOLLIN | EPOLLET);
    channel->getLoop().lock()->updatePoller(channel);
}

