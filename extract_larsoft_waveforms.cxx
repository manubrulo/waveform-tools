#include <chrono>
#include <functional>
#include <iostream>
#include <string>
#include <vector>
#include <fstream>
#include <sstream>

#include "boost/program_options.hpp"

#include "canvas/Utilities/InputTag.h"
#include "gallery/Event.h"

#include "lardataobj/Simulation/SimChannel.h"
#include "lardataobj/RawData/RawDigit.h"
#include "lardataobj/RawData/RDTimeStamp.h"

#include "cnpy.h"

using namespace art;
using namespace std;
using namespace std::chrono;

namespace po = boost::program_options;

enum class Format { Text, Numpy };


template<class T>
void save_to_file(std::string const& outfile,
                  std::vector<std::vector<T> > v,
                  Format format,
                  bool append)
{
    switch(format){

    case Format::Text:
    {
        // Open in append mode because we 
        std::ofstream fout(outfile, append ? ios::app : ios_base::out);
        for(auto const& v1 : v){
            for(auto const& s : v1){
                fout << s << " ";
            }
            fout << std::endl;
        }
    }
    break;
    
    case Format::Numpy:
    {
        // Do nothing if the vector is empty
        if(v.empty() || v[0].empty()) break;
        // cnpy needs a single contiguous array of data, so do that conversion
        std::vector<T> tmp;
        tmp.reserve(v.size()*v[0].size());
        for(auto const& v1 : v){
            for(auto const& s : v1){
                tmp.push_back(s);
            }
        }
        cnpy::npy_save(outfile, &tmp[0], {v.size(), v[0].size()}, append ? "a" : "w");
    }
    break;
    }
}

// Write `nevents` events of data from `filename` to text files. The
// raw waveforms are written to `outfile`, while the true energy
// depositions are written to `truth_outfile` (unless it is an empty
// string, in which case no truth file is produced). If `onlySignal`
// is true, then only channels with some true energy deposition are
// written out; otherwise all channels are written out.
// 
// Each line in `outfile` has the format:
//
// event_no channel_no sample_0 sample_1 ... sample_N
//
// Each line in `truth_outfile` has the format
//
// event_no channel_no tdc total_charge
void
extract_larsoft_waveforms(std::string const& tag,
                          std::string const& filename,
                          std::string const& outfile,
                          std::string const& truth_outfile,
                          Format format,
                          int nevents, int nskip, bool onlySignal,
                          int triggerType)
{
  InputTag daq_tag{ tag };
  // Create a vector of length 1, containing the given filename.
  vector<string> filenames(1, filename);

  int iev=0;
  for (gallery::Event ev(filenames); !ev.atEnd(); ev.next()) {
    vector<vector<int> > samples;
    vector<vector<float> > trueIDEs;

    std::set<int> channelsWithSignal;
    if(iev<nskip) continue;
    if(iev>=nevents+nskip) break;
    if(triggerType!=-1){
        auto& timestamp=*ev.getValidHandle<std::vector<raw::RDTimeStamp>>(InputTag{"timingrawdecoder:daq:DecoderandReco"});
        assert(timestamp.size()==1);
        if(timestamp[0].GetFlags()!=triggerType){
            std::cout << "Skipping event " << ev.eventAuxiliary().event()  << " with trigger type " << timestamp[0].GetFlags() << std::endl;
            continue;
        }
        else{
            std::cout << "Using event " << ev.eventAuxiliary().event()  << " with trigger type " << timestamp[0].GetFlags() << std::endl;
        }
    }
    std::cout << "Event " << ev.eventAuxiliary().id() << std::endl;
    if(truth_outfile!=""){
        //------------------------------------------------------------------
        // Get the SimChannels so we can see where the actual energy depositions were
        auto& simchs=*ev.getValidHandle<std::vector<sim::SimChannel>>(InputTag{"largeant"});
        
        for(auto&& simch: simchs){
            channelsWithSignal.insert(simch.Channel());
            if(truth_outfile!=""){
                double charge=0;
                for (const auto& TDCinfo: simch.TDCIDEMap()) {
                    for (const sim::IDE& ide: TDCinfo.second) {
                        charge += ide.numElectrons;
                    } // for IDEs
                    auto const tdc = TDCinfo.first;
                    trueIDEs.push_back(std::vector<float>{(float)iev, (float)simch.Channel(), (float)tdc, (float)charge});
                } // for TDCs
            } // if fout_truth
        } // loop over SimChannels
    }

    int waveform_nsamples=-1;
    int n_truncated=0;
    //------------------------------------------------------------------
    // Look at the digits (ie, TPC waveforms)
    auto& digits =
      *ev.getValidHandle<vector<raw::RawDigit>>(daq_tag);
    if(digits.empty()){
        std::cout << "Digits vector is empty" << std::endl;
    }
    for(auto&& digit: digits){
      if(digit.Compression()!=0){
        std::cout << "Compression type " << digit.Compression() << std::endl;
        exit(1);
      }
      if(onlySignal && channelsWithSignal.find(digit.Channel())==channelsWithSignal.end()){
        continue;
      }
      // Check that the waveform has the same number of samples as all the previous waveforms
      if(waveform_nsamples<0){ waveform_nsamples=digit.NADC(); }
      else{
          if(digit.NADC()!=waveform_nsamples){
              if(n_truncated<10){
                  std::cerr << "Channel " << digit.Channel() << " (offline APA " << (digit.Channel()/2560) << ") has " << digit.NADC() << " samples but all previous channels had " << waveform_nsamples << " samples" << std::endl;
              }
              if(n_truncated==100){
                  std::cerr << "(More errors suppressed)" << std::endl;
              }
              ++n_truncated;
          }
      }
      samples.push_back({(int)ev.eventAuxiliary().event(), (int)digit.Channel()});
      for(size_t i=0; i<waveform_nsamples; ++i){
          int sample=i<digit.NADC() ? digit.ADCs()[i] : digit.ADCs().back();
          samples.back().push_back(sample);
      }
    } // end loop over digits (=?channels)
    if(n_truncated!=0){
        std::cerr << "Truncated " << n_truncated << " channels with the wrong number of samples" << std::endl;
    }
    std::string this_outfile(outfile);
    size_t dotpos=outfile.find_last_of(".");
    if(dotpos==std::string::npos){
      dotpos=outfile.length();
    }
    std::ostringstream iss;
    iss << outfile.substr(0, dotpos) << "_evt" << ev.eventAuxiliary().event() << outfile.substr(dotpos, outfile.length()-dotpos);
    std::cout << "Writing event " << ev.eventAuxiliary().event() << " to file " << iss.str() << std::endl;
    save_to_file<int>(iss.str(), samples, format, false);
    if(truth_outfile!="") save_to_file<float>(truth_outfile, trueIDEs, format, iev!=0);
    ++iev;
  } // end loop over events
}

int main(int argc, char** argv)
{
    po::options_description desc("Allowed options");
    desc.add_options()
        ("help,h", "produce help message")
        ("input,i", po::value<string>(), "input file name")
        ("output,o", po::value<string>(), "base output file name. Individual output files will be created for each event, with \"_evtN\" inserted before the extension, or at the end if there is no extension")
        ("truth,t", po::value<string>()->default_value(""), "truth output file name")
        ("tag,g", po::value<string>()->default_value("daq"), "input tag (aka \"module label\") of input digits")
        ("nevent,n", po::value<int>()->default_value(1), "number of events to save")
        ("nskip,k", po::value<int>()->default_value(0), "number of events to skip")
        ("numpy", "use numpy output format instead of text")
        ("onlysignal", "only output channels with true signal")
        ("trig", po::value<int>()->default_value(-1), "select events with given trigger type")
        ;

    po::variables_map vm;
    po::store(po::parse_command_line(argc, argv, desc), vm);
    po::notify(vm);    

    if(vm.count("help") || vm.empty()) {
        cout << desc << "\n";
        return 1;
    }

    if(!vm.count("input")){
        cout << "No input file specified" << endl;
        cout << desc << endl;
        return 1;
    }

    if(!vm.count("output")){
        cout << "No output file specified" << endl;
        cout << desc << endl;
        return 1;
    }

    extract_larsoft_waveforms(vm["tag"].as<string>(),
                              vm["input"].as<string>(),
                              vm["output"].as<string>(),
                              vm["truth"].as<string>(),
                              vm.count("numpy") ? Format::Numpy : Format::Text,
                              vm["nevent"].as<int>(),
                              vm["nskip"].as<int>(),
                              vm.count("onlysignal"),
                              vm["trig"].as<int>());
    return 0;
}
