#include "NetioTxCore.h"
#include "NetioTools.h"
#include <cstdint>
#include <iostream>
#include <iomanip>
#include <chrono>
#include "NetioFei4Records.h"
#include "felixbase/client.hpp"

using namespace std;
using namespace netio;

DECLARE_TXCORE(NetioTxCore)

NetioTxCore::NetioTxCore() : m_nioh{NetioHandler::getInstance()} {
  m_enableMask = 0;
  m_trigEnabled = false;
  m_trigWordLength = 4;
  m_trigCnt = 0;
  m_trigFreq = 1;
  m_extend = 4;
  m_padding = true;
  m_flip = true;
  m_verbose = false;
  m_debug = false;
  m_manchester = false;

  m_felixhost = "localhost";
  m_felixport = 12340;

  m_context = new context("posix");
  m_socket = new low_latency_send_socket(m_context);
}

NetioTxCore::~NetioTxCore(){
  if(m_trigProc.joinable()) m_trigProc.join();
  if(m_socket->is_open()) m_socket->disconnect();
  delete m_socket;
  delete m_context;
}

mutex & NetioTxCore::getMutex(){
  return m_mutex;
}

void NetioTxCore::connect(){
  if(!m_socket->is_open()){
    try{
      m_socket->connect(netio::endpoint(m_felixhost,m_felixport));
      cout << "Connected to " << m_felixhost << ":" << m_felixport << endl;
    }catch(...){
      cout << "Cannot connect to " << m_felixhost << ":" << m_felixport << endl;
    }
  }
}

void NetioTxCore::enableChannel(uint64_t elink){
  if(m_verbose) cout << "Enable TX elink: 0x" << hex << elink << dec << endl;
  m_elinks[elink]=true;
}

void NetioTxCore::disableChannel(uint64_t elink){

  m_elinks[elink]=false;
  if(m_elinks.find(elink)==m_elinks.end()){return;}
  
  // We don't disable channels...
  if(m_verbose) cout << "Disable TX elink: 0x" << hex << elink << dec << endl;  
  
  //m_elinks[elink]=false;
  //if(m_verbose) cout << "Disable TX elink: 0x" << hex << elink << dec << endl;
}

void NetioTxCore::writeFifo(std::string hexStr){
  if(m_debug) cout << "NetioTxCore::writeFifo str=0x" << hex << hexStr << dec << endl;
  map<uint64_t,bool>::iterator it;

  for(it=m_elinks.begin();it!=m_elinks.end();it++){
    writeFifo(it->first,hexStr);
  }
}

void NetioTxCore::writeFifo(uint32_t value){
  if(m_debug) cout << "NetioTxCore::writeFifo val=" << hex << setw(8) << setfill('0') << value << dec << endl;
  map<uint64_t,bool>::iterator it;

  for(it=m_elinks.begin();it!=m_elinks.end();it++){
    writeFifo(it->first,value);
  }
}

void NetioTxCore::writeFifo(uint32_t chn, std::string hexStr){
  if(m_debug) cout << "NetioTxCore::writeFifo chn=" << chn << " str=0x" << hex << hexStr << dec << endl;
  uint64_t elink=chn;
  m_fifoBits.Clear();
  m_fifoBits.FromHex(hexStr);
  m_fifoBits.Pack();
  for (uint32_t i=0; i<m_fifoBits.GetSize(); ++i){
    writeFifo(elink,m_fifoBits.GetWord(i));
  }
}

void NetioTxCore::writeFifo(uint32_t chn, uint32_t value){
  if(m_debug) cout << "NetioTxCore::writeFifo chn=" << chn 
                   << " val=0x" << hex << setw(8) << setfill('0') << value << dec << endl;
  uint64_t elink=chn;
  writeFifo(&m_fifo[elink],value);  
}

void NetioTxCore::writeFifo(vector<uint8_t> *fifo, uint32_t value){
  if(m_extend==4){
    for(int32_t b=3;b>=0;b--){
      for(int32_t i=0;i<4;i++){
        uint32_t val=(value>>((b+1)*8-i*2-2))&0x3;
        if     (val==0){fifo->push_back(0x00);}
        else if(val==1){fifo->push_back(0x0F);}
        else if(val==2){fifo->push_back(0xF0);}
        else if(val==3){fifo->push_back(0xFF);}
      }
    }
  }else{
    for(int32_t b=3;b>=0;b--){
      fifo->push_back((value>>b*8)&0xFF);
    }
  }
}

void NetioTxCore::prepareFifo(vector<uint8_t> *fifo){

  if(m_padding==true){ 
    if(m_debug) cout << "Padding" << endl;
    uint32_t i0=0;
    if(m_debug){cout << "Find the first byte" << endl;}
    for(uint32_t i=1; i<fifo->size(); i++){
      if(fifo->at(i)!=0){i0=i-1;break;}
    }
    if(m_debug){cout << "Copy the array forward" << endl;}
    for(uint32_t i=0; i<fifo->size()-i0; i++){
      fifo->at(i)=fifo->at(i+i0);
    }
    if(m_debug){cout << "Remove first " << i0 << " characters" << endl;}
    for(uint32_t i=0; i<i0; i++){
      fifo->pop_back();
    }
    if(m_debug){cout << "Pop back" << endl;}  
  }
  
  if(m_flip==true){
    if(m_debug) cout << "Flipping" << endl;
    if(fifo->size()%2==1){
      fifo->push_back(0);
    }
    for(uint32_t i=0; i<fifo->size(); i++){//was i=1
      uint32_t tmp = fifo->at(i);
      fifo->at(i) = ((tmp&0xF0) >> 4) | ((tmp&0x0F)<<4);
    }
  }

  if(m_manchester==true){
    if(m_debug) cout << "Manchester" << endl;
    bool clk=true;
    fifo->insert(fifo->begin(),2,0x0); //16 extra leading zeroes
    for(uint32_t i=0; i<fifo->size(); i++){
      uint32_t tmp = fifo->at(i);      
      fifo->at(i) = (tmp&0xCC && clk) | (tmp&0x33 && !clk);
    }
  }

}

void NetioTxCore::releaseFifo(uint32_t chn){
  uint64_t elink=chn;

  if(m_debug) cout << "NetioTxCore::releaseFifo chn=" << chn << endl;

  //try to connect
  connect();

  //pad and flip
  prepareFifo(&m_fifo[elink]);
  if(m_debug) printFifo(elink);
  
  //prepare the data
  m_headers[elink].elinkid=elink;
  m_headers[elink].length=m_fifo[elink].size();
  m_data.push_back((uint8_t*)&(m_headers[elink]));
  m_size.push_back(sizeof(felix::base::ToFELIXHeader));
  m_data.push_back((uint8_t*)&m_fifo[elink][0]);
  m_size.push_back(m_fifo[elink].size());	

  if(m_debug) cout << "m_data size " << m_data.size() << " " << "fifo size " << m_fifo[elink].size() << endl;

  //create the netio message
  message msg(m_data,m_size);
  //Send through the socket
  m_socket->send(msg);

  m_fifo[elink].clear();
  m_size.clear(); 
  m_data.clear();

}

void NetioTxCore::releaseFifo(){

  //try to connect
  connect();
  
  if(m_debug) cout << "NetioTxCore::releaseFifo " << endl;
  //create the message for NetIO
  map<uint64_t,bool>::iterator it;
    
  for(it=m_elinks.begin();it!=m_elinks.end();it++){
    if(it->second==false) continue;
    prepareFifo(&m_fifo[it->first]);
    if(m_debug) printFifo(it->first);
    m_headers[it->first].elinkid=it->first;
    m_headers[it->first].length=m_fifo[it->first].size();
    m_data.push_back((uint8_t*)&(m_headers[it->first]));
    m_size.push_back(sizeof(felix::base::ToFELIXHeader));
    m_data.push_back((uint8_t*)&m_fifo[it->first][0]);
    m_size.push_back(m_fifo[it->first].size());	
  }

  message msg(m_data,m_size);
   
  //Send through the socket
  m_socket->send(msg);

  for(it=m_elinks.begin();it!=m_elinks.end();it++){
    if(it->second==false) continue;
    m_fifo[it->first].clear();
  }
  m_size.clear(); 
  m_data.clear();

}

void NetioTxCore::trigger(){

  //try to connect
  connect();
  
  map<tag,felix::base::ToFELIXHeader> headers; 
  vector<const uint8_t*> data;
  vector<size_t> size;

  if(m_debug) cout << "NetioTxCore::trigger " << endl;
  //create the message for NetIO
  map<uint64_t,bool>::iterator it;
    
  for(it=m_elinks.begin();it!=m_elinks.end();it++){
    if(it->second==false) continue;
    //prepareFifo(&m_trigFifo[it->first]);
    headers[it->first].elinkid=it->first;
    headers[it->first].length=m_trigFifo[it->first].size();
    data.push_back((uint8_t*)&(headers[it->first]));
    size.push_back(sizeof(felix::base::ToFELIXHeader));
    data.push_back((uint8_t*)&m_trigFifo[it->first][0]);
    size.push_back(m_trigFifo[it->first].size());	
  }

  message msg(data,size);
   
  //Send through the socket
  m_socket->send(msg);

  for(it=m_elinks.begin();it!=m_elinks.end();it++){
    if(it->second==false) continue;
    //m_trigFifo[it->first].clear();
  }
  size.clear(); 
  data.clear();

}

bool NetioTxCore::isCmdEmpty(){ 
  map<uint64_t,bool>::iterator it;
  for(it=m_elinks.begin();it!=m_elinks.end();it++){
    if(it->second==false) continue;
    if(!m_fifo[it->first].empty()) return false;
  }
  return true;
}

void NetioTxCore::setTrigEnable(uint32_t value){
  if(value == 0) {
    if(m_trigProc.joinable()) m_trigProc.join();
  } else {
    m_trigEnabled = true;
    switch (m_trigCfg) {
    case INT_TIME:
    case EXT_TRIGGER:
      m_trigProc = std::thread(&NetioTxCore::doTriggerTime, this);
      break;
    case INT_COUNT:
      m_trigProc = std::thread(&NetioTxCore::doTriggerCnt, this);
      break;
    default:
      // Should not occur, else stuck
      break;
    }
  }
}

uint32_t NetioTxCore::getTrigEnable(){
  return m_trigEnabled;
}

void NetioTxCore::setTrigChannel(uint64_t elink, bool enable){
  m_trigElinks[elink]=enable;
}

void NetioTxCore::toggleTrigAbort(){
  m_trigEnabled = false;
  //Abort trigger -> Empty the CircularBuffer + ensure stable 0 size?
}

bool NetioTxCore::isTrigDone(){ 
  if (!m_trigEnabled && isCmdEmpty()){ return true; }
  return false;
}

void NetioTxCore::setTrigConfig(enum TRIG_CONF_VALUE cfg){
  m_trigCfg = cfg;
}

void NetioTxCore::setTrigFreq(double freq){
  m_trigFreq=freq;
}

void NetioTxCore::setTrigCnt(uint32_t count){
  m_trigCnt = count;
}

uint32_t NetioTxCore::getTrigCnt(){
  return m_trigCnt;
}

void NetioTxCore::setTrigTime(double time){
  m_trigTime = time;
}

double NetioTxCore::getTrigTime(){
  return m_trigTime;
}

void NetioTxCore::setTrigWordLength(uint32_t length){
  m_trigWordLength=length;
}

void NetioTxCore::setTrigWord(uint32_t *word, uint32_t size){ 
  m_trigWords.clear();
  for(uint32_t i=0;i<size;i++){m_trigWords.push_back(word[i]);}
}

void NetioTxCore::setTriggerLogicMask(uint32_t mask){
  //Nothing to do yet
}

void NetioTxCore::setTriggerLogicMode(enum TRIG_LOGIC_MODE_VALUE mode){
  //Nothing to do yet
}

void NetioTxCore::resetTriggerLogic(){
  //Nothing to do yet
}

uint32_t NetioTxCore::getTrigInCount(){
  return 0;
}

void NetioTxCore::prepareTrigger(){
  for(auto it=m_elinks.begin();it!=m_elinks.end();it++){
    m_trigFifo[it->first].clear();
    for(int32_t j=3; j>=0;j--){
      writeFifo(&m_trigFifo[it->first],m_trigWords[j]);
    }
    writeFifo(&m_trigFifo[it->first],0x0);
    prepareFifo(&m_trigFifo[it->first]);
  }
}
  
void NetioTxCore::doTriggerCnt() {
  
  prepareTrigger();
  
  uint32_t trigs=0;
  for(uint32_t i=0; i<m_trigCnt; i++) {
    if(m_trigEnabled==false) break;    
    trigs++;
    trigger();
    std::this_thread::sleep_for(std::chrono::microseconds((int)(1e6/m_trigFreq))); // Frequency in Hz
  }
  m_trigEnabled = false;
  if(m_verbose) cout << "finished trigger count" << endl;
  if(m_debug){
    cout << endl
         << "============> Finish trigger with n counts: " << trigs << endl
         << endl;
  }
}

void NetioTxCore::doTriggerTime() {
  
  //PrepareTrigger
  prepareTrigger();

  chrono::steady_clock::time_point start = chrono::steady_clock::now();
  chrono::steady_clock::time_point cur = start;
  uint32_t trigs=0;
  while (chrono::duration_cast<chrono::seconds>(cur-start).count() < m_trigTime) {
    if(m_trigEnabled==false) break;
    trigs++;
    trigger();
    this_thread::sleep_for(chrono::microseconds((int)(1000/m_trigFreq))); // Frequency in kHz
    cur = chrono::steady_clock::now();
  }
  m_trigEnabled = false;
  if(m_verbose) cout << "finished trigger time" << endl;
  if(m_debug){
    cout << endl
         << "============> Finish trigger with n counts: " << trigs << endl
         << endl;
  }
}

void NetioTxCore::printFifo(uint64_t elink){
  cout << "FIFO[" << elink << "][" << m_fifo[elink].size()-1 << "]: " << hex;
  for(uint32_t i=1; i<m_fifo[elink].size(); i++){
    cout << setfill('0') << setw(2) << (uint32_t)(m_fifo[elink][i]&0xFF);
  }
  cout << dec << endl;
}

void NetioTxCore::setVerbose(bool enable){
  m_verbose = enable;
}

void NetioTxCore::toString(string &s) {}

void Tokenize(const string& str,
              vector<string>& tokens,
              const string& delimiters = " "){
  // Skip delimiters at beginning.
  string::size_type lastPos = str.find_first_not_of(delimiters, 0);
  // Find first "non-delimiter".
  string::size_type pos     = str.find_first_of(delimiters, lastPos);
  
  while (string::npos != pos || string::npos != lastPos){
    // Found a token, add it to the vector.
    tokens.push_back(str.substr(lastPos, pos - lastPos));
    // Skip delimiters.  Note the "not_of"
    lastPos = str.find_first_not_of(delimiters, pos);
    // Find next "non-delimiter"
    pos = str.find_first_of(delimiters, lastPos);
  }
}

void NetioTxCore::fromString(string s) {
  //Decode "host:port"
  /* size_t pos1 = 0;
  size_t pos2 = s.find("{");
  if(pos2!=string::npos){
    pos1=pos2+1;
  }
  pos2 = s.find(":",pos1);
  m_felixhost = s.substr(pos1,pos2-pos1);
  pos1 = pos2;
  m_felixport = atoi(s.substr(pos1+1).c_str());
  */
  std::vector<string> tokens;
  Tokenize(s,tokens,":");
  if(tokens.size()>0) m_felixhost = tokens[0];
  if(tokens.size()>1) m_felixport = atoi(tokens[1].c_str());
  if(tokens.size()>2) m_extend = atoi(tokens[2].c_str());
  if(tokens.size()>3) m_flip = atoi(tokens[3].c_str());
  if(tokens.size()>4) m_manchester = atoi(tokens[4].c_str());
  cout << "NetioTxCore: " << endl
       << " manchester=" << m_manchester << endl
       << " flip=" << m_flip << endl
       << " extend=" << m_extend << endl;
}

void NetioTxCore::toFileJson(json &j)  {}

void NetioTxCore::fromFileJson(json &j){
   m_felixhost  = j["NetIO"]["host"];
   m_felixport  = j["NetIO"]["txport"];
   m_manchester = j["NetIO"]["manchester"];
   m_flip       = j["NetIO"]["flip"];
   m_extend     = (j["NetIO"]["extend"]?4:1);

   cout << "NetioTxCore: " << endl
        << " manchester=" << m_manchester << endl
        << " flip=" << m_flip << endl
        << " extend=" << m_extend << endl;
     
   
}

