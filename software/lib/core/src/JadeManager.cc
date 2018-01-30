#include "JadeManager.hh"

using namespace std::chrono_literals;

JadeManager::JadeManager(const std::string &dev_path)
  :m_dev_path(dev_path), m_is_running(false){
}

JadeManager::~JadeManager(){
  m_is_running = false;
  if(m_fut_async_rd.valid())
    m_fut_async_rd.get();
  if(m_fut_async_dcd.valid())
    m_fut_async_dcd.get();
  if(m_fut_async_flt.valid())
    m_fut_async_flt.get();
  if(m_fut_async_wrt.valid())
    m_fut_async_wrt.get();
  std::cout<<"JadeManager: deleted"<<std::endl;
}

uint64_t JadeManager::AsyncReading(){
  uint64_t n_df = 0;
  while (m_is_running){
    auto v_df = m_rd->Read(10, 10ms);
    std::unique_lock<std::mutex> lk_out(m_mx_ev_to_dcd);
    for(auto &&df: v_df){
      m_qu_ev_to_dcd.push(std::move(df));
      n_df ++;
    }    
    lk_out.unlock();
    m_cv_valid_ev_to_dcd.notify_all();
  }
  return n_df;
}

uint64_t JadeManager::AsyncDecoding(){
  uint64_t n_df = 0;
  while(m_is_running){
    std::unique_lock<std::mutex> lk_in(m_mx_ev_to_dcd);
    if(m_qu_ev_to_dcd.empty()){
      while(m_cv_valid_ev_to_dcd.wait_for(lk_in, 1s) ==
	    std::cv_status::timeout){
	if(!m_fut_async_rd.valid()){
	  return 0;
	}
      }
    }
    auto df = std::move(m_qu_ev_to_dcd.front());
    m_qu_ev_to_dcd.pop();
    lk_in.unlock();
    df->Decode();
    std::unique_lock<std::mutex> lk_out(m_mx_ev_to_dcd);
    m_qu_ev_to_flt.push(std::move(df));
    n_df ++;
    lk_out.unlock();
    m_cv_valid_ev_to_flt.notify_all();   
  }
  return n_df;
}

uint64_t JadeManager::AsyncFiltering(){
  uint64_t n_df = 0;
  while(m_is_running){
    std::unique_lock<std::mutex> lk_in(m_mx_ev_to_flt);
    if(m_qu_ev_to_flt.empty()){
      while(m_cv_valid_ev_to_flt.wait_for(lk_in, 1s) ==
	    std::cv_status::timeout){
	if(!m_is_running){
	  return 0;
	}
      }
    }
    auto df = std::move(m_qu_ev_to_flt.front());
    m_qu_ev_to_flt.pop();
    lk_in.unlock();
    auto df_r = m_flt->Filter(std::move(df));
    if(df_r){
      std::unique_lock<std::mutex> lk_out(m_mx_ev_to_wrt);
      m_qu_ev_to_flt.push(std::move(df_r));
      n_df ++;
      lk_out.unlock();
      m_cv_valid_ev_to_wrt.notify_all();
    }
  }
  return n_df;
}

uint64_t JadeManager::AsyncWriting(){
  uint64_t n_df = 0;
  while(m_is_running){
    std::unique_lock<std::mutex> lk_in(m_mx_ev_to_wrt);
    if(m_qu_ev_to_wrt.empty()){
      while(m_cv_valid_ev_to_wrt.wait_for(lk_in, 1s)
	    ==std::cv_status::timeout){
	if(!m_is_running){
	  return 0;
	}
      }
    }
    auto df = std::move(m_qu_ev_to_wrt.front());
    m_qu_ev_to_wrt.pop();
    lk_in.unlock();
    m_wrt->Write(std::move(df));
    n_df ++;
  }
  return n_df;
}

void JadeManager::Start(const std::string &file_in,
			const std::string &file_out){
  m_is_running = true;
  m_rd.reset(new JadeRead(file_in, ""));
  m_fut_async_rd = std::async(std::launch::async,
			      &JadeManager::AsyncReading, this);
  
  m_flt.reset(new JadeFilter(""));
  m_fut_async_flt = std::async(std::launch::async,
			       &JadeManager::AsyncFiltering, this);
  
  m_wrt.reset(new JadeWrite(file_out, ""));
  m_fut_async_wrt = std::async(std::launch::async,
			       &JadeManager::AsyncWriting, this);
}
