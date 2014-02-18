#include "BigQ.h"

RecordWrapper::RecordWrapper(Record* recordToCopy,OrderMaker* sortOrder)
{
	(this->record).Copy(recordToCopy);
	this->sortOrder = sortOrder;
}

int RecordWrapper::compareRecords(const void* rw1, const void *rw2)
{
	RecordWrapper* recWrap1 = ((RecordWrapper*) rw1);
	RecordWrapper* recWrap2 = ((RecordWrapper*) rw2);
	OrderMaker* sortOrder =  ((RecordWrapper*) rw1)->sortOrder;
	ComparisonEngine compEngine;
	return (compEngine.Compare(&(recWrap1->record),&(recWrap2->record),sortOrder) < 0);
}

ComparisonClass:: ComparisonClass()
{
	compEngine = new ComparisonEngine();
}
bool ComparisonClass:: operator()(const pair<Record*,int> &lhs, const pair<Record*,int> &rhs)
{
	Record* r1 = lhs.first;
	Record* r2 = rhs.first;

}

BigQ :: BigQ (Pipe &in, Pipe &out, OrderMaker &sortorder, int runlen): inputPipe(in),outputPipe(out),sortOrder(sortorder) {
	// read data from in pipe sort them into runlen pages
	runLength = runlen;
	int rc = pthread_create(&worker,NULL,workerFunc,(void*)this);
	if(rc){
		cerr<<"Not able to create worker thread"<<endl;
		exit(1);
	}
    // construct priority queue over sorted runs and dump sorted data 
 	// into the out pipe

    // finally shut down the out pipe
	//out.ShutDown ();
}

BigQ::~BigQ () {
	pthread_join(worker, NULL);
}
/*
 * @method
 */
void* workerFunc(void *bigQ)
{
	BigQ *bq  = (BigQ*) bigQ;
	File* file = new File();
	file->Open(0,"temp.dat");
	Pipe& in = bq->inputPipe;
	Pipe& out = bq->outputPipe;
	int runlen = bq->runLength;
	//create file of sorted runs.
	createRuns(runlen,in,file,(bq->sortOrder));
	cout<<"File ki length after writting "<<file->GetLength()<<endl;
	//once a file is created of sorted runs merge each of the run.
	mergeRunsFromFile(file,runlen,out,(bq->sortOrder));
	file->Close();
	out.ShutDown ();
}
/**
 * @method createRuns to create a file of sorted runs and number of runs.
 * @returns total number of runs created.
 *
 */
void createRuns(int runlen, Pipe& in, File *file,OrderMaker& sortOrder)
{
	Record* currentRecord = new Record();
	Page page;
	vector<RecordWrapper*> list;
	RecordWrapper *tempWrapper;
	int i=0;
	int numPages = 0;
	while(in.Remove(currentRecord) !=0)
	{
		//create a copy of record in recordWrapper which will be pushed to List for sorting.
		tempWrapper = new RecordWrapper(currentRecord,&sortOrder);
		if(page.Append(currentRecord) == 0)
		{
			//when unable to add to page implies page is full so increament number of pages.
			numPages++;
			//check if number of pages read is equivalent to number of pages specified in runlength.
			if(numPages == runlen)
			{
				sortAndCopyToFile(list,file);
				//clean the list for next run.
				list.clear();
				//set numPages to 0 again.
				numPages =0;
			}
			page.EmptyItOut();
			page.Append(currentRecord);
		}
		list.push_back(tempWrapper);
	}
	delete currentRecord;
	//If there are more records on list which needs to be written on disk
	if(list.size() >0)
		sortAndCopyToFile(list,file);
	list.clear();
	in.ShutDown();
}
/**
 * @method sortAndCopyToFile to sort the given vector list of records and then append it to end of file. Method maintains a static count of
 * pages written to file which is used as offset to add page to file.
 * @param List of type vector<RecordWrapper*> a reference to list of Records wrapped in RecordWrapper.
 * @param File Pointer to File to add pages to end of the same file.
 *
 */
void sortAndCopyToFile(vector<RecordWrapper*>& list,File* file)
{
	static off_t nextPageMarker = 0;
	Page page;
	//First Sort the vector and then add the records in sorted order in file.
	sort(list.begin(), list.end(), RecordWrapper::compareRecords);
	//Write the sorted records to file.
	for(std::vector<RecordWrapper*>::iterator iter = list.begin() ; iter != list.end(); ++iter)
	{
		Record tempRec  =  (*iter)->record;
		if(page.Append(&tempRec) == 0)
		{
			file->AddPage(&page,nextPageMarker);
			//Increament the next Page Marker.
			nextPageMarker++;
			//Empty the page and write the failed Record.
			page.EmptyItOut();
			page.Append(&tempRec);
		}
	}
	file->AddPage(&page,nextPageMarker);
	nextPageMarker++;

	page.EmptyItOut();
}
/**
 * @method writeRunToFile to write records read from input pipe to File as sorted runs.
 * @param File* pointer to File where records need to be written.
 * @param vector<Record> list of Records to be written to file.
 * 
 */
void writeRunToFile(File* file, vector<Record*> &list)
{
	Page* page = new Page();
	//To mark if there are records in page which needs to be written on file.
	for(int i=0;i<list.size();i++)
	{
		Record* record = list[i];
		int status = page->Append(record);
		//if record was not added to page i.e. page was full.
		if(status == 0)
		{
			off_t offSet = file->GetLength();
			if(offSet != 0)
				offSet--;
			file->AddPage(page,offSet);
			page->EmptyItOut();
			//append the record to new page.
			page->Append(record);
		}
	}
	off_t offSet = file->GetLength();
	if(offSet != 0)
		offSet--;
	file->AddPage(page,offSet);
	page->EmptyItOut();
	delete page;
}
/**
 * 
 */
void mergeRunsFromFile(File* file, int runLength,Pipe& out,OrderMaker& orderMaker)
{
	int fileLength = file->GetLength()-1;
	int numRuns = ceil(fileLength*1.0f/runLength);
	std::priority_queue<pair<Record*,int>, std::vector<pair<Record*,int> >,ComparisonClass> priorityQueue;
	cout<<"NumRuns ::: "<<numRuns<<endl;
	cout<<"FileLength ::"<<fileLength<<endl;

	//Array of Pages to keep hold of current Page from each of run.
	Page* pageBuffers = new Page[numRuns]();
	//initialise each page with corresponding page in File.
	vector<off_t> offset;
	//initialise offset array to keep track of next page for each run.
	//Initialise each of the Page Buffers with the first page of each run.
	for(int i=0;i<fileLength;i+=runLength)
	    offset.push_back(i);

	cout<<"Offset ki size :: "<<offset.size()<<endl;	
	for(int i=0;i<offset.size();i++)
	{
		//Get the Page for offset in Buffer.
		file->GetPage(&(pageBuffers[i]),offset[i]);
		//increament offset for run
		offset[i] = offset[i] + 1;
		//For each of the page get the first record in priority queue.
		Record* record = new Record();
		pageBuffers[i].GetFirst(record);
		priorityQueue.push(make_pair(record,i));
	}
	for(int i=0;i<offset.size();i++)
	{
		cout<<"offset for run " <<i<<" offset : "<<offset[i]<<endl;
	}
	/*
	Record* record;
	while(!priorityQueue.empty())
	{
		pair<Record*, int> topPair = priorityQueue.top();
		record = topPair.first;
		int runNum = topPair.second;
		priorityQueue.pop();
		//Write record to output pipe.
		out.Insert(record);
		cout<<"Run Number :: "<<runNum<<endl;
		//Get the next record from Record Num and add it to priorityQueue.
		Record* recordToInsert = new Record();
		if(pageBuffers[runNum].GetFirst(recordToInsert) == 0)
		{
			cout<<"ab khatam hua"<<endl;
			continue;
		}
		priorityQueue.push(make_pair(recordToInsert,runNum));
	}
	*/
	//while priority queue is not empty keeping popping records from Priority Queue and write it to pipe.
	Record* record;
	while(!priorityQueue.empty())
	{
		pair<Record*, int> topPair = priorityQueue.top();
		record = topPair.first;
		int runNum = topPair.second;
		priorityQueue.pop();
		//Write record to output pipe.
		out.Insert(record);
		//Get the next record from Record Num and add it to priorityQueue.
		Record* recordToInsert = new Record();
		if(pageBuffers[runNum].GetFirst(recordToInsert) == 0)
		{
			//if no records were found from the page try to get the next page if page exists in the same run.
			off_t currOffset = offset[runNum];
			//only if there are more pages in run read the next page else skip.
			cout<<"run Number ::"<<runNum<<"\tcurrOffset::"<<currOffset<<"\trunLength:: "<<runLength<<"\tfileLength :: "<<fileLength<<endl;
			if(currOffset%runLength != 0 && currOffset < fileLength)
			{
				cout<<"IF"<<endl;	
				file->GetPage(&pageBuffers[runNum],currOffset);
				//increament offset after reading page from current offset for run
				offset[runNum]++;	
				if(pageBuffers[runNum].GetFirst(recordToInsert) == 0)
					continue;
			}
			else
			{
				cout<<"Else"<<endl;
				//No more Pages to read
				continue;
			}
		}//no else block required since it has no more pages to read.
		priorityQueue.push(make_pair(recordToInsert,runNum));
	}

}
