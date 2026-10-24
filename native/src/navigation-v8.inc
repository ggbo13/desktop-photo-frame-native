 uint64_t navigationSequence=0,latestNavigationTag=0;
 std::optional<size_t> logicalHistoryCursor;
 std::map<uint64_t,size_t> historyRequests;
 size_t latestPhotoTarget()const{return frame.queued?frame.pendingIndex:(frame.animating?frame.incomingIndex:((frame.loading&&!frame.prefetching)?frame.pendingIndex:frame.index));}
 void resetNavigation(){logicalHistoryCursor.reset();historyRequests.clear();latestNavigationTag=0;}
 void navigate(int direction){
  if(frame.paths.empty())return;
  size_t base=latestPhotoTarget(),target=base;std::optional<size_t> destination,reversedPending;
  size_t cursor=logicalHistoryCursor.value_or(historyCursor);
  if(direction<0){
   uint64_t pendingTag=frame.queued?frame.pendingTag:(frame.animating?frame.incomingTag:frame.requestTag);
   if(!logicalHistoryCursor&&base!=frame.index&&latestNavigationTag&&pendingTag==latestNavigationTag&&stringValue(store.active()["config"],"order")!="random"){reversedPending=(base+frame.paths.size()-1)%frame.paths.size();if(*reversedPending==frame.index&&!history.empty())destination=historyCursor;}
   else if(!logicalHistoryCursor&&base!=frame.index&&!history.empty())destination=historyCursor;
   else if(cursor>0)destination=cursor-1;else return;
  }else if((logicalHistoryCursor||base==frame.index)&&!history.empty()&&cursor+1<history.size())destination=cursor+1;
  if(destination){auto it=std::find(frame.paths.begin(),frame.paths.end(),history[*destination]);if(it==frame.paths.end())return;target=size_t(it-frame.paths.begin());logicalHistoryCursor=destination;}
  else if(reversedPending){target=*reversedPending;logicalHistoryCursor.reset();}
  else{
   logicalHistoryCursor.reset();
   if(stringValue(store.active()["config"],"order")!="random")target=(base+1)%frame.paths.size();
   else{
    if(bag.empty())resetOrder();auto it=std::find(bag.begin(),bag.end(),base);
    if(it!=bag.end()&&std::next(it)!=bag.end())target=*std::next(it);
    else if(!bag.empty()&&bag.front()!=base)target=bag.front();
    else{std::vector<size_t> remaining;for(size_t i=0;i<frame.paths.size();++i)if(i!=base)remaining.push_back(i);std::shuffle(remaining.begin(),remaining.end(),random);bag.assign(remaining.begin(),remaining.end());target=bag.empty()?base:bag.front();}
   }
  }
  uint64_t tag=++navigationSequence;latestNavigationTag=tag;if(destination)historyRequests[tag]=*destination;
  frame.request(target,false,false,direction,tag);
  if(!frame.animating&&!frame.loading&&target==frame.index){if(destination)historyCursor=*destination;logicalHistoryCursor.reset();historyRequests.clear();}
 }
 void recordPresented(size_t i){
  auto request=historyRequests.find(frame.presentedTag);
  if(request!=historyRequests.end()&&request->second<history.size()&&history[request->second]==frame.paths[i])historyCursor=request->second;
  else{if(!history.empty()&&historyCursor+1<history.size())history.resize(historyCursor+1);if(history.empty()||history.back()!=frame.paths[i])history.push_back(frame.paths[i]);if(history.size()>512){history.erase(history.begin());for(auto& [tag,cursor]:historyRequests)if(cursor>0)--cursor;}historyCursor=history.empty()?0:history.size()-1;}
  if(frame.presentedTag==latestNavigationTag)logicalHistoryCursor.reset();
  for(auto it=historyRequests.begin();it!=historyRequests.end();)if(it->first!=latestNavigationTag)it=historyRequests.erase(it);else ++it;
 }
