#include <iostream>
#include <cstdio>      // using fread for faster file reading
#include <string>
#include <unordered_map>
#include <vector>
#include <algorithm>
#include <stdexcept>
#include <ctime>

using namespace std;


// small helper just to print some stats at the end
// template so it works for int, double etc
template <typename T>
void printStat(string name, T val)
{
    cout << name << ": " << val << "\n";
}



// TOKENIZER
// this class takes raw characters and converts them into words
// since the file is read in chunks, sometimes a word may break
// between two buffers. So we keep the unfinished part in `rem`
// and attach it to the next chunk.

class Tokenizer
{
private:
    string rem;   // leftover part of a word

public:

    Tokenizer()
    {
        rem = "";
        rem.reserve(256);   // reserve some space to avoid resizing repeatedly
    }


    void processWords(const vector<char> &buf, int n, bool end,
                      unordered_map<string,int> &wc)
    {
        char word[1024];   // temporary place to build the current word
        int wlen = 0;

        // if the previous chunk ended in the middle of a word,
        // rem stores that partial word. We start building the new word from there. 
        for(char c : rem)
        {
            word[wlen++] = c;
        }

        for(int i=0;i<n;i++)
        {
            char ch = buf[i];

            // convert uppercase letters to lowercase
            if(ch >= 'A' && ch <= 'Z')
            {
                word[wlen++] = ch + 32;
            }

            // normal letters or digits continue the word
            else if((ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9'))
            {
                word[wlen++] = ch;
            }

            // punctuation or spaces mean the word ended
            else
            {
                if(wlen > 0)
                {
                    wc[string(word,wlen)]++;
                    wlen = 0;
                }
            }
        }

        // if this chunk was the end of file, finalize the last word
        if(end && wlen > 0)
        {
            wc[string(word,wlen)]++;
            rem = "";
        }
        else
        {
            // otherwise keep unfinished word for the next chunk
            rem = string(word,wlen);
        }
    }

    // if the file ended exactly at the buffer boundary,
    // the last word might still be sitting in rem.
    // this ensures that it gets counted.
    void finish(unordered_map<string,int> &wc)
    {
        if(!rem.empty())
        {
            wc[rem]++;
            rem.clear();
        }
    }
};



// VERSIONED INDEX
// stores word counts for each version
// structure: version -(word - count)

class VersionedIndexer
{
private:
    unordered_map<string, unordered_map<string,int>> data;

public:

    // returns frequency of a word in a version
    int getFreq(string ver, string w)
    {
        // using find() so we don't accidentally insert a new word
        // when checking its frequency
        auto v = data.find(ver);
        if(v == data.end()) return 0;

        auto wIt = v->second.find(w);
        if(wIt == v->second.end()) return 0;

        return wIt->second;
    }

    // overloaded version: returns number of unique words in that version
    int getFreq(string ver)
    {
        return data[ver].size();
    }

    unordered_map<string,int>& getMap(string ver)
    {
        return data[ver];
    }
};



// FILE READER
// reads the file in chunks using fread()
// this is faster than reading character-by-character

class BufferedFileReader
{
private:
    string file;
    int buf;
    Tokenizer tok;

public:

    BufferedFileReader(string f, int kb)
    {
        if(kb < 256 || kb > 1024)
        {
            throw invalid_argument("buffer must be between 256 and 1024");
        }

        file = f;
        buf = kb * 1024;
    }


    void readFile(VersionedIndexer &idx, string ver)
    {
        FILE *fp = fopen(file.c_str(),"rb");

        if(!fp)
        {
            throw runtime_error("cannot open file: " + file);
        }

        vector<char> chunk(buf);

        unordered_map<string,int> &wc = idx.getMap(ver);

        // reserve space so the map doesn't rehash too often
        wc.reserve(500000);

        int n;

        while((n = fread(chunk.data(),1,buf,fp)) > 0)
        {
            bool end = feof(fp);

            tok.processWords(chunk,n,end,wc);
        }

        // flush any leftover word that was split at buffer boundary
        tok.finish(wc);

        fclose(fp);
    }
};



// BASE QUERY CLASS
// all queries inherit from this base class
// each derived class implements execute()

class QueryProcessor
{
protected:
    VersionedIndexer *idx;

public:

    QueryProcessor(VersionedIndexer *p)
    {
        idx = p;
    }

    virtual void execute() = 0;

    virtual ~QueryProcessor(){}
};



// WORD COUNT QUERY
// prints how many times a word appears in a version

class WordCountQuery : public QueryProcessor
{
private:
    string ver;
    string w;

public:

    WordCountQuery(VersionedIndexer *p,string v,string word) : QueryProcessor(p)
    {
        ver = v;
        w = word;
    }

    void execute()
    {
        int f = idx->getFreq(ver,w);

        cout << "Version: " << ver << "\n";
        cout << "Count: " << f << "\n";
    }
};



// DIFF QUERY
// compares word frequency between two versions

class DiffQuery : public QueryProcessor
{
private:
    string v1,v2,w;

public:

    DiffQuery(VersionedIndexer *p,string a,string b,string word) : QueryProcessor(p)
    {
        v1 = a;
        v2 = b;
        w = word;
    }

    void execute()
    {
        int c1 = idx->getFreq(v1,w);
        int c2 = idx->getFreq(v2,w);

        cout << "Difference (" << v2 << " - " << v1 << "): "
             << (c2 - c1) << "\n";
    }
};



// TOP K QUERY
// prints top K most frequent words

class TopKQuery : public QueryProcessor
{
private:
    string ver;
    int k;

public:

    TopKQuery(VersionedIndexer *p,string v,int top) : QueryProcessor(p)
    {
        ver = v;
        k = top;
    }

    void execute()
    {
        // printing top-k header
        cout << "Top-" << k << " words in version " << ver << ":\n";

        unordered_map<string,int> &mp = idx->getMap(ver);

        vector<pair<int,string>> arr;

        // move map data into a vector so we can sort
        for(auto it = mp.begin(); it != mp.end(); it++)
        {
            arr.push_back(make_pair(it->second,it->first));
        }

        // sort words by frequency (descending)
        sort(arr.begin(), arr.end(),
             [](pair<int,string> a, pair<int,string> b)
             {
                 if(a.first == b.first)
                     return a.second < b.second;

                 return a.first > b.first;
             });

        int lim = min(k,(int)arr.size());

        for(int i=0;i<lim;i++)
        {
            cout << arr[i].second << " " << arr[i].first << "\n";
        }
    }
};



// MAIN FUNCTION

int main(int argc,char *argv[])
{
    ios::sync_with_stdio(false);
    cin.tie(NULL);

    clock_t start = clock();

    string f1,f2,v1,v2,q,w;

    int buf = 512;
    int k = 0;

    // reading command line arguments
    for(int i=1;i<argc;i++)
    {
        string a = argv[i];

        if(a=="--file" || a=="--file1") f1 = argv[++i];
        else if(a=="--file2") f2 = argv[++i];
        else if(a=="--version" || a=="--version1") v1 = argv[++i];
        else if(a=="--version2") v2 = argv[++i];
        else if(a=="--buffer") buf = stoi(argv[++i]);
        else if(a=="--query") q = argv[++i];
        else if(a=="--top") k = stoi(argv[++i]);
        else if(a=="--word")
        {
            w = argv[++i];

            // convert query word to lowercase
            transform(w.begin(),w.end(),w.begin(),::tolower);
        }
    }

    try
    {
        VersionedIndexer idx;

        QueryProcessor *qp = nullptr;

        if(q=="word" || q=="top")
        {
            BufferedFileReader r(f1,buf);

            r.readFile(idx,v1);

            if(q=="word")
                qp = new WordCountQuery(&idx,v1,w);
            else
                qp = new TopKQuery(&idx,v1,k);
        }

        else if(q=="diff")
        {
            BufferedFileReader r1(f1,buf);
            BufferedFileReader r2(f2,buf);

            r1.readFile(idx,v1);
            r2.readFile(idx,v2);

            qp = new DiffQuery(&idx,v1,v2,w);
        }

        if(qp != nullptr)
        {
            qp->execute();
            delete qp;
        }

        printStat("Buffer Size (KB)",buf);
    }

    catch(exception &e)
    {
        cout << "Error: " << e.what() << "\n";
    }

    clock_t end = clock();

    double t = double(end-start)/CLOCKS_PER_SEC;

    printStat("Execution Time (s)",t);

    return 0;
}
