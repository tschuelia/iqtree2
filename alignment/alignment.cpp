//
// C++ Implementation: alignment
//
// Description:
//
//
// Author: BUI Quang Minh, Steffen Klaere, Arndt von Haeseler <minh.bui@univie.ac.at>, (C) 2008
//
// Copyright: See COPYING file that comes with this distribution
//
//
#include "alignment.h"
#include "nclextra/myreader.h"
#include <numeric>
#include <sstream>
#include "model/rategamma.h"
#include "gsl/mygsl.h"
#include <utils/gzstream.h>
#include <utils/hammingdistance.h> //for sumForUnknownCharacters
#include <utils/progress.h>        //for progress_display
#include <utils/safe_io.h>         //for safeGetLine
#include <utils/stringfunctions.h> //for convert_int, etc.
#include <utils/timeutil.h>        //for getRealTime()
#include <utils/tools.h>
#include "alignmentsummary.h"
#include <tree/phylotree.h>

#include <Eigen/LU>
#ifdef USE_BOOST
#include <boost/math/distributions/binomial.hpp>
#endif

#ifdef _MSC_VER
#include <boost/scoped_array.hpp>
#endif

using namespace std;
using namespace Eigen;

char symbols_protein[] = "ARNDCQEGHILKMFPSTWYVX"; // X for unknown AA
char symbols_dna[]     = "ACGT";
char symbols_rna[]     = "ACGU";
//char symbols_binary[]  = "01";
char symbols_morph[] = "0123456789ABCDEFGHIJKLMNOPQRSTUV";
// genetic code from tri-nucleotides (AAA, AAC, AAG, AAT, ..., TTT) to amino-acids
// Source: http://www.ncbi.nlm.nih.gov/Taxonomy/Utils/wprintgc.cgi
// Base1:                AAAAAAAAAAAAAAAACCCCCCCCCCCCCCCCGGGGGGGGGGGGGGGGTTTTTTTTTTTTTTTT
// Base2:                AAAACCCCGGGGTTTTAAAACCCCGGGGTTTTAAAACCCCGGGGTTTTAAAACCCCGGGGTTTT
// Base3:                ACGTACGTACGTACGTACGTACGTACGTACGTACGTACGTACGTACGTACGTACGTACGTACGT
const char genetic_code1[]  = "KNKNTTTTRSRSIIMIQHQHPPPPRRRRLLLLEDEDAAAAGGGGVVVV*Y*YSSSS*CWCLFLF"; // Standard
const char genetic_code2[]  = "KNKNTTTT*S*SMIMIQHQHPPPPRRRRLLLLEDEDAAAAGGGGVVVV*Y*YSSSSWCWCLFLF"; // Vertebrate Mitochondrial
const char genetic_code3[]  = "KNKNTTTTRSRSMIMIQHQHPPPPRRRRTTTTEDEDAAAAGGGGVVVV*Y*YSSSSWCWCLFLF"; // Yeast Mitochondrial
const char genetic_code4[]  = "KNKNTTTTRSRSIIMIQHQHPPPPRRRRLLLLEDEDAAAAGGGGVVVV*Y*YSSSSWCWCLFLF"; // Mold, Protozoan, etc.
const char genetic_code5[]  = "KNKNTTTTSSSSMIMIQHQHPPPPRRRRLLLLEDEDAAAAGGGGVVVV*Y*YSSSSWCWCLFLF"; // Invertebrate Mitochondrial
const char genetic_code6[]  = "KNKNTTTTRSRSIIMIQHQHPPPPRRRRLLLLEDEDAAAAGGGGVVVVQYQYSSSS*CWCLFLF"; // Ciliate, Dasycladacean and Hexamita Nuclear
// note: tables 7 and 8 are not available in NCBI
const char genetic_code9[]  = "NNKNTTTTSSSSIIMIQHQHPPPPRRRRLLLLEDEDAAAAGGGGVVVV*Y*YSSSSWCWCLFLF"; // Echinoderm and Flatworm Mitochondrial
const char genetic_code10[] = "KNKNTTTTRSRSIIMIQHQHPPPPRRRRLLLLEDEDAAAAGGGGVVVV*Y*YSSSSCCWCLFLF"; // Euplotid Nuclear
const char genetic_code11[] = "KNKNTTTTRSRSIIMIQHQHPPPPRRRRLLLLEDEDAAAAGGGGVVVV*Y*YSSSS*CWCLFLF"; // Bacterial, Archaeal and Plant Plastid
const char genetic_code12[] = "KNKNTTTTRSRSIIMIQHQHPPPPRRRRLLSLEDEDAAAAGGGGVVVV*Y*YSSSS*CWCLFLF"; // Alternative Yeast Nuclear
const char genetic_code13[] = "KNKNTTTTGSGSMIMIQHQHPPPPRRRRLLLLEDEDAAAAGGGGVVVV*Y*YSSSSWCWCLFLF"; // Ascidian Mitochondrial
const char genetic_code14[] = "NNKNTTTTSSSSIIMIQHQHPPPPRRRRLLLLEDEDAAAAGGGGVVVVYY*YSSSSWCWCLFLF"; // Alternative Flatworm Mitochondrial
const char genetic_code15[] = "KNKNTTTTRSRSIIMIQHQHPPPPRRRRLLLLEDEDAAAAGGGGVVVV*YQYSSSS*CWCLFLF"; // Blepharisma Nuclear
const char genetic_code16[] = "KNKNTTTTRSRSIIMIQHQHPPPPRRRRLLLLEDEDAAAAGGGGVVVV*YLYSSSS*CWCLFLF"; // Chlorophycean Mitochondrial
// note: tables 17-20 are not available in NCBI
const char genetic_code21[] = "NNKNTTTTSSSSMIMIQHQHPPPPRRRRLLLLEDEDAAAAGGGGVVVV*Y*YSSSSWCWCLFLF"; // Trematode Mitochondrial
const char genetic_code22[] = "KNKNTTTTRSRSIIMIQHQHPPPPRRRRLLLLEDEDAAAAGGGGVVVV*YLY*SSS*CWCLFLF"; // Scenedesmus obliquus mitochondrial
const char genetic_code23[] = "KNKNTTTTRSRSIIMIQHQHPPPPRRRRLLLLEDEDAAAAGGGGVVVV*Y*YSSSS*CWC*FLF"; // Thraustochytrium Mitochondrial
const char genetic_code24[] = "KNKNTTTTSSKSIIMIQHQHPPPPRRRRLLLLEDEDAAAAGGGGVVVV*Y*YSSSSWCWCLFLF"; // Pterobranchia mitochondrial
const char genetic_code25[] = "KNKNTTTTRSRSIIMIQHQHPPPPRRRRLLLLEDEDAAAAGGGGVVVV*Y*YSSSSGCWCLFLF"; // Candidate Division SR1 and Gracilibacteria

const char* genetic_codes[] = {
    //Translation tables not available in NCBI... nulptr.
    nullptr, 
    genetic_code1,  genetic_code2,  genetic_code3,  genetic_code4,
    genetic_code5,  genetic_code6,  nullptr,        nullptr,
    genetic_code9,  genetic_code10, genetic_code11, genetic_code12,
    genetic_code13, genetic_code14, genetic_code15, genetic_code16,
    nullptr,        nullptr,        nullptr,        nullptr,
    genetic_code21, genetic_code22, genetic_code23, genetic_code24,
    genetic_code25
};
const int min_translation_table = 1;
const int max_translation_table = 25;

Alignment::Alignment()
        : vector<Pattern>()
{
    num_states = 0;
    frac_const_sites = 0.0;
    frac_invariant_sites = 0.0;
    seq_type = SeqType::SEQ_UNKNOWN;
    STATE_UNKNOWN = 126;
    pars_lower_bound = nullptr;
    isShowingProgressDisabled = false;
    virtual_pop_size = 0;
    num_parsimony_sites = 0;
    num_variant_sites = 0;
    num_informative_sites = 0;
}

const string &Alignment::getSeqName(intptr_t i) const {
    ASSERT(i >= 0 && i < (int)seq_names.size());
    return seq_names[i];
}

void Alignment::setSeqName(intptr_t i, const string& name_to_use) {
    ASSERT(i >= 0 && i < (int)seq_names.size());
    seq_names[i] = name_to_use;
}

const StrVector& Alignment::getSeqNames() const {
	return seq_names;
}

intptr_t Alignment::getMapFromNameToID(NameToIDMap& answer) const {
    intptr_t before = answer.size();
    for (intptr_t i = 0; i < getNSeq(); i++) {
        answer[seq_names[i]] = i;
    }
    return answer.size() - before;
}

intptr_t Alignment::getSeqID(const string &seq_name) const {
    for (intptr_t i = 0; i < getNSeq(); i++) {
        if (seq_name == getSeqName(i)) {
            return i;
        }
    }
    return -1;
}

size_t Alignment::getMaxSeqNameLength() const {
    size_t len = 0;
    for (intptr_t i = 0; i < getNSeq(); ++i) {
        if (getSeqName(i).length() > len) {
            len = getSeqName(i).length();
        }
    }
    return len;
}

int Alignment::getSequenceSubset(intptr_t i) const {
    ASSERT(0<=i && i < (int)seq_names.size());
    if (seq_to_subset.size() <= i) {
        return 0;
    }
    return seq_to_subset[i];
}

void Alignment::setSequenceSubset(intptr_t i, intptr_t set_no) {
    ASSERT(i >= 0 && i < (int)seq_names.size());
    if (seq_to_subset.size() <= i) {
        seq_to_subset.resize(i+1, 0);
    }
    seq_to_subset[i] = set_no;
}

/**
   probability that the observed chi-square exceeds chi2 even if model is correct
   @param deg degree of freedom
   @param chi2 chi-square value
   @return p-value
   */
double chi2prob (int deg, double chi2)
{
    double a = 0.5*deg;
    double x = 0.5*chi2;
    return 1.0-RateGamma::cmpIncompleteGamma (x, a, RateGamma::cmpLnGamma(a));
//	return IncompleteGammaQ (0.5*deg, 0.5*chi2);
} /* chi2prob */


int Alignment::checkAbsentStates(const string& msg) {
    std::vector<double> state_freq_vector(num_states);
    double *state_freq = state_freq_vector.data();
    computeStateFreq(state_freq, 0, nullptr);
    string absent_states, rare_states;
    int count = 0;
    // Skip check for PoMo.
    if (seq_type == SeqType::SEQ_POMO)
      return 0;
    for (int i = 0; i < num_states; i++)
        if (state_freq[i] == 0.0) {
            if (!absent_states.empty()) {
                absent_states += ", ";
            }
            absent_states += convertStateBackStr(i);
            count++;
        } else if (state_freq[i] <= Params::getInstance().min_state_freq) {
            if (!rare_states.empty()) {
                rare_states += ", ";
            }
            rare_states += convertStateBackStr(i);
        }
    if (count >= num_states-1 &&
        Params::getInstance().fixed_branch_length != BRLEN_FIX) {
        outError("Only one state is observed in " + msg);
    }
    if (!absent_states.empty()) {
        cout << "NOTE: State(s) " << absent_states
             << " not present in " << msg
             << " and thus removed from Markov process"
             << " to prevent numerical problems" << endl;
    }
    if (!rare_states.empty()) {
        cout << "WARNING: States(s) " << rare_states
             << " rarely appear in " << msg
             << " and may cause numerical problems" << endl;
    }
    return count;
}

void Alignment::checkSeqName() {
    renameSequencesIfNeedBe();
    checkSequenceNamesAreDistinct();
    if (!Params::getInstance().compute_seq_composition) {
        return;
    }

#ifndef _MSC_VER
    double state_freq[num_states];
#else
    boost::scoped_array<double> state_freq(new double[num_states]);
#endif
    unsigned *count_per_seq = new unsigned[num_states*getNSeq()];
    computeStateFreq(&state_freq[0], 0, nullptr);
    countStatePerSequence(count_per_seq);

    int df = -1; //degrees of freedom (for a chi-squared test)
    for (int i = 0; i < num_states; i++) {
        if (state_freq[i] > 0.0) {
            df++;
        }
    }    
    if (seq_type == SeqType::SEQ_POMO) {
        std::cout << "NOTE: The composition test for PoMo"
             << " only tests the proportion of fixed states!" << endl;
    }
    bool   listSequences = !Params::getInstance().suppress_list_of_sequences;
    size_t max_len       = getMaxSeqNameLength()+1;
    if (listSequences) {
        std::cout.width(max_len+14);
        std::cout << right << "Gap/Ambiguity" 
                  << "  Composition  p-value" << endl;
        std::cout.precision(2);
    }

    AlignmentSummary s(this, true, true);
    s.constructSequenceMatrixNoisily(false, "Analyzing sequences",
                                     "counted gaps in");
    //The progress bar, displayed by s.constructSequenceMatrixNoisily,
    //lies a bit here.  We're not counting gap characters,
    //we are constructing the sequences (so we can count gap characters quickly).

    size_t     num_problem_seq;
    size_t     total_gaps;
    size_t     num_failed;
    SequenceInfo* seqInfo        = calculateSequenceInfo(&s, &state_freq[0],
                                                         count_per_seq, df,
                                                         num_problem_seq,
                                                         total_gaps, num_failed);
    
    if (listSequences) {
        reportSequenceInfo(seqInfo, max_len);
    }

    forgetSequenceInfo(seqInfo);
    
    if (num_problem_seq) {
        std::cout << "WARNING: " << num_problem_seq
                  << " sequences contain more than 50% gaps/ambiguity" << endl;
    }
    if (listSequences) {
        std::cout << "**** ";
        std::cout.width(max_len+2);
        std::cout << left << " TOTAL  ";
        std::cout.width(6);
        std::cout << right << ((double)total_gaps/getNSite())/getNSeq()*100 << "% ";
        std::cout << " " << num_failed << " sequences failed composition"
                  << " chi2 test (p-value<5%; df=" << df << ")" << endl;
        std::cout.precision(3);
    }
    delete [] count_per_seq;
}

void Alignment::renameSequencesIfNeedBe() {
    ostringstream warn_str;
    StrVector::iterator it;
    for (it = seq_names.begin(); it != seq_names.end(); ++it) {
        string orig_name = (*it);
        if (renameString(*it)) {
            warn_str << orig_name << " -> " << (*it) << endl;
        }
    }
    if (!warn_str.str().empty() &&
        Params::getInstance().compute_seq_composition) {
        string str = "Some sequence names are changed as follows:\n";
        outWarning(str + warn_str.str());
    }
}

void Alignment::checkSequenceNamesAreDistinct() {
    // now check that sequence names are different
    StrVector names(seq_names);
    names.sort();
    bool ok = true;
    for (auto it = names.begin(); it != names.end(); it++) {
        if (it+1==names.end()) break;
        if (*it == *(it+1)) {
            std::cout << "ERROR: Duplicated sequence name " << *it << endl;
            ok = false;
        }
    }
    if (!ok) {
        outError("Please rename sequences listed above!");
    }
}

SequenceInfo* Alignment::calculateSequenceInfo(const AlignmentSummary* s,
                                               const double*   state_freq,
                                               const unsigned* count_per_seq,
                                               int     degrees_of_freedom,
                                               size_t &r_num_problem_seq,
                                               size_t &r_total_gaps, 
                                               size_t &r_num_failed) {
    intptr_t      numSequences = seq_names.size();
    char     firstUnknownState = static_cast<char>(num_states + pomo_sampled_states.size());
    SequenceInfo* seqInfo      = new SequenceInfo[numSequences];

    size_t num_problem_seq     = 0;
    size_t total_gaps          = 0;
    size_t num_failed          = 0;
    #ifdef _OPENMP
    #pragma omp parallel for reduction(+:total_gaps,num_problem_seq,num_failed)
    #endif
    for (int i = 0; i < numSequences; i++) {
        size_t num_gaps = countGapsInSequence(s, firstUnknownState, i);
        total_gaps += num_gaps;
        seqInfo[i].percent_gaps = ((double)num_gaps / getNSite()) * 100.0;
        if ( 50.0 < seqInfo[i].percent_gaps ) {
            num_problem_seq++;
        }
        size_t iRow = i * num_states;
#ifndef _MSC_VER
        double freq_per_sequence[num_states];
#else
        boost::scoped_array<double> freq_per_sequence(new double[num_states]);
#endif
        double chi2 = 0.0;
        unsigned sum_count = 0;
        double pvalue;
        if (seq_type == SeqType::SEQ_POMO) {
            // Have to normalize allele frequencies.
#ifndef _MSC_VER
            double state_freq_norm[num_states];
#else
            boost::scoped_array<double> state_freq_norm(new double[num_states]);
#endif
            double sum_freq = 0.0;
            for (int j = 0; j < num_states; j++) {
                sum_freq += state_freq[j];
                state_freq_norm[j] = state_freq[j];
            }
            for (int j = 0; j < num_states; j++) {
                state_freq_norm[j] /= sum_freq;
            }
            for (int j = 0; j < num_states; j++) {
                sum_count += count_per_seq[iRow + j];
            }
            double sum_inv = 1.0 / sum_count;
            for (int j = 0; j < num_states; j++) {
                freq_per_sequence[j] = count_per_seq[iRow + j] * sum_inv;
            }
            for (int j = 0; j < num_states; j++) {
                chi2 += (state_freq_norm[j] - freq_per_sequence[j]) * (state_freq_norm[j] - freq_per_sequence[j]) / state_freq_norm[j];
            }
            chi2  *= sum_count;
            pvalue = chi2prob(num_states - 1, chi2);
        }
        else {
            for (int j = 0; j < num_states; j++) {
                sum_count += count_per_seq[iRow + j];
            }
            double sum_inv = 1.0 / sum_count;
            for (int j = 0; j < num_states; j++) {
                freq_per_sequence[j] = count_per_seq[iRow + j] * sum_inv;
            }
            for (int j = 0; j < num_states; j++) {
                if (state_freq[j] > 0.0) {
                    chi2 += (state_freq[j] - freq_per_sequence[j]) * (state_freq[j] - freq_per_sequence[j]) / state_freq[j];
                }
            }
            chi2  *= sum_count;
            pvalue = chi2prob(degrees_of_freedom, chi2);
        }
        seqInfo[i].pvalue = pvalue;
        seqInfo[i].failed = (pvalue < 0.05);
        num_failed += seqInfo[i].failed ? 1 : 0;
    }
    r_num_problem_seq = num_problem_seq;
    r_total_gaps      = total_gaps;
    r_num_failed      = num_failed;
    return seqInfo;
}

size_t Alignment::countGapsInSequence(const AlignmentSummary* s, 
                                      char  firstUnknownState, 
                                      int   seq_index) const {
    if (s->hasSequenceMatrix()) {
        //Discount the non-gap characters with a (not-yet-vectorized)
        //sweep over the sequence.
        const char* sequence    = s->getSequence(seq_index);
        const int*  frequencies = s->getSiteFrequencies().data();
        size_t      seq_len     = s->getSequenceLength();


        return sumForUnknownCharacters(firstUnknownState, sequence,
                                        seq_len, frequencies);
    } else {
        //Do the discounting the hard way
        return getNSite() - countProperChar(seq_index);
    }
}

void Alignment::reportSequenceInfo(const SequenceInfo* seqInfo,
                                   size_t max_len) const {
    intptr_t numSequences = seq_names.size();
    for (intptr_t i = 0; i < numSequences; i++) {
        std::cout.width(4);
        std::cout << right << i + 1 << "  ";
        std::cout.width(max_len);
        std::cout << left << seq_names[i] << " ";
        std::cout.width(6);
        std::cout << right << seqInfo[i].percent_gaps << "%";
        if (seqInfo[i].failed) {
            std::cout << "    failed ";
        }
        else {
            std::cout << "    passed ";
        }
        std::cout.width(9);
        std::cout << right << (seqInfo[i].pvalue * 100) << "%";
        std::cout << endl;
    }
}

void Alignment::forgetSequenceInfo(SequenceInfo*& seqInfo) const {
    delete [] seqInfo;
    seqInfo = nullptr;
}

int Alignment::checkIdenticalSeq()
{
    //Todo: This should use sequence hashing.
	int num_identical = 0;
    IntVector checked;
    intptr_t nseq = getNSeq(); 
    checked.resize(nseq, 0);
	for (intptr_t seq1 = 0; seq1 < nseq; ++seq1) {
        if (checked[seq1]) continue;
		bool first = true;
		for (intptr_t seq2 = seq1+1; seq2 < nseq; ++seq2) {
			bool equal_seq = true;
			for (iterator it = begin(); it != end(); ++it)
				if  ((*it)[seq1] != (*it)[seq2]) {
					equal_seq = false;
					break;
				}
			if (equal_seq) {
                if (first) {
                    std::cout << "WARNING: Identical sequences " << getSeqName(seq1);
                }
                std::cout << ", " << getSeqName(seq2);
				num_identical++;
				checked[seq2] = 1;
				first = false;
			}
		}
		checked[seq1] = 1;
        if (!first) {
            std::cout << endl;
        }
	}
    if (num_identical) {
        outWarning("Some identical sequences found"
                   " that should be discarded before the analysis");
    }
    return num_identical;
}

vector<size_t> Alignment::getSequenceHashes(progress_display_ptr progress) const {
    auto startHash = getRealTime();
    auto n = getNSeq();
    vector<size_t> hashes;
    hashes.resize(n, 0);
    #ifdef _OPENMP
        #pragma omp parallel for schedule(static,100)
    #endif
    for (intptr_t seq1=0; seq1<n; ++seq1) {
        size_t hash = 0;
        for (auto it = begin(); it != end(); ++it) {
            adjustHash((*it)[seq1], hash);
        }
        hashes[seq1] = hash;
        if (progress!=nullptr && (n%100)==99) {
            (*progress) += (100.0);
        }
    }
    if (progress!=nullptr) {
        (*progress) += (n%100);
    }
    
#if USE_PROGRESS_DISPLAY
    bool displaying_progress = progress_display::getProgressDisplay();
#else
    const bool displaying_progress = false;
#endif
    
    if (verbose_mode >= VerboseMode::VB_MED && !displaying_progress) {
        auto hashTime = getRealTime() - startHash;
        std::cout << "Hashing sequences took " << hashTime
                  << " wall-clock seconds" << endl;
    }
    return hashes;
}

vector<size_t> Alignment::getPatternIndependentSequenceHashes(progress_display_ptr progress) const {
    auto n = getNSeq();
    vector<size_t> hashes;
    hashes.resize(n, 0);
    
    auto patterns     = site_pattern.data();
    auto patternCount = site_pattern.size();
    #ifdef _OPENMP
        #pragma omp parallel for schedule(static,100)
    #endif
    for (intptr_t seq1=0; seq1<n; ++seq1) {
        size_t hash = 0;
        for (int i=0; i<patternCount; ++i) {
            adjustHash(at(patterns[i])[seq1], hash);
        }
        hashes[seq1] = hash;
        if (progress!=nullptr && (n%100)==99) {
            (*progress) += 100.0;
        }
    }
    return hashes;
}

template <class V, class C> void getCounts(const std::vector<V>& values, 
                                           std::map<V, C>& counts) {
    for (const V& v: values) {
        auto it = counts.find(v);
        if (it==counts.end()) {
            counts[v] = 1;
        } else {
            ++counts[v];
        }
    }
}

Alignment *Alignment::removeIdenticalSeq(string not_remove, bool keep_two,
                                         StrVector &removed_seqs, StrVector &target_seqs)
{
    auto n = getNSeq();
    BoolVector isSequenceChecked(n, false);
    BoolVector isSequenceRemoved(n, false);
    
#if USE_PROGRESS_DISPLAY
    const char* task_name = isShowingProgressDisabled 
                          ? "" :  "Checking for duplicate sequences";
    progress_display progress(n*1.1, task_name);
#else
    double progress = 0.0;
#endif
    
    intptr_t       nseq   = getNSeq();
    vector<size_t> hashes = getSequenceHashes(&progress);
    std::map<size_t, size_t> hash_counts;
    getCounts(hashes, hash_counts);

    bool listIdentical = !Params::getInstance().suppress_duplicate_sequence_warnings;

    auto startCheck = getRealTime();
    for (intptr_t seq1 = 0; seq1 < nseq; ++seq1) {
        if ((seq1%1000)==999) {
            progress += 1000.0;
        }
        if ( isSequenceChecked[seq1] || hash_counts[hashes[seq1]] == 1) {
            continue;
        }
        bool first_ident_seq = true;
        for (intptr_t seq2 = seq1+1; seq2 < nseq; ++seq2) {
            if (!shouldRemoveSequence(seq1, seq2, not_remove,
                                      isSequenceRemoved, hashes)) {
                continue;
            }
            if (static_cast<int>(removed_seqs.size())+3 < getNSeq() &&
                (!keep_two || !first_ident_seq)) {
                removed_seqs.push_back(getSeqName(seq2));
                target_seqs.push_back(getSeqName(seq1));
                isSequenceRemoved[seq2] = true;
            } else {
                reportSequenceKept(seq1, seq2, listIdentical, progress);
            }
            isSequenceChecked[seq2] = true;
            first_ident_seq         = false;
        }
        isSequenceChecked[seq1] = true;
    }
    doneCheckingForDuplicateSequences(startCheck, progress);

    if (0 < removed_seqs.size() ) {
        return removeSpecifiedSequences(removed_seqs, isSequenceRemoved);
    } else {
        return this;
    }
}

bool Alignment::shouldRemoveSequence(intptr_t seq1, intptr_t seq2, 
                                     const string& not_remove,
                                     const BoolVector& isSequenceRemoved,
                                     const vector<size_t>& hashes) const {
    if ( getSeqName(seq2) == not_remove || 
            isSequenceRemoved[seq2] ) {
        return false;
    }
    if (hashes[seq1] != hashes[seq2]) {
        return false; //JB2020-06-17
    }
    for (auto it = begin(); it != end(); it++) {
        if  ((*it)[seq1] != (*it)[seq2]) {
            return false;
        }
    }
    return true;
}

void Alignment::reportSequenceKept(intptr_t seq1, intptr_t seq2, bool listIdentical,
                                   progress_display& progress) const {
    if (listIdentical) {
        #if USE_PROGRESS_DISPLAY
        progress.hide();
        #endif
        std::cout << "NOTE: " << getSeqName(seq2)
                  << " is identical to " << getSeqName(seq1)
                  << " but kept for subsequent analysis" << endl;
        #if USE_PROGRESS_DISPLAY
        progress.show();
        #endif
    }
}

void Alignment::doneCheckingForDuplicateSequences(double startCheck, 
                                                  progress_display& progress) const {
    #if USE_PROGRESS_DISPLAY
        bool displaying_progress = progress_display::getProgressDisplay();
    #else
        const bool displaying_progress = false;
    #endif
    if (verbose_mode >= VerboseMode::VB_MED && !displaying_progress) {
        auto checkTime = getRealTime() - startCheck;
        std::cout << "Checking for duplicate sequences took " << checkTime
                  << " wall-clock seconds" << endl;
    }
    #if USE_PROGRESS_DISPLAY
        progress.done();
    #endif
}

Alignment* Alignment::removeSpecifiedSequences
                (const StrVector&  removed_seqs,
                 const BoolVector& isSequenceRemoved) {
    double   removeDupeStart = getRealTime();
    intptr_t nseq            = getNSeq();
    if (static_cast<intptr_t>(removed_seqs.size()) + 3 >= nseq) {
        outWarning("Your alignment contains too many identical sequences!");
    }
    IntVector keep_seqs;
    for (intptr_t seq1 = 0; seq1 < nseq; seq1++) {
        if ( !isSequenceRemoved[seq1] ) {
            keep_seqs.emplace_back(static_cast<int>(seq1));
        }
    }
    Alignment* aln = new Alignment;
    aln->extractSubAlignment(this, keep_seqs, 0);
    //std::cout << "NOTE: Identified " << removed_seqs.size()
    //          << " sequences as duplicates." << endl;
    if (verbose_mode >= VerboseMode::VB_MED) {
        std::stringstream msg;
        msg.precision(4);
        msg << "Removing " << removed_seqs.size() << " duplicated sequences took "
            << (getRealTime() - removeDupeStart) << " sec.";
        std::cout << msg.str() << std::endl;
    }
    return aln;
}

void Alignment::adjustHash(StateType v, size_t& hash) const {
    //Based on what boost::hash_combine() does.
    //For now there's no need for a templated version
    //in a separate header file.  But if other classes start
    //wanting to "roll their own hashing" this should move
    //to, say, utils/hashing.h.
    hash ^= std::hash<int>()(v) + 0x9e3779b9
                     + (hash<<6) + (hash>>2);
}
void Alignment::adjustHash(bool v, size_t& hash) const {
    hash ^= std::hash<bool>()(v) + 0x9e3779b9
                     + (hash<<6) + (hash>>2);
}

bool Alignment::isGapOnlySeq(intptr_t seq_id) {
    ASSERT(seq_id < getNSeq());
    for (iterator it = begin(); it != end(); ++it) {
        if ((*it)[seq_id] != STATE_UNKNOWN) {
            return false;
        }
    }
    return true;
}

Alignment *Alignment::removeGappySeq() {
    IntVector keep_seqs;
    intptr_t    nseq = getNSeq();
	for (intptr_t i = 0; i < nseq; i++)
		if (! isGapOnlySeq(i)) {
			keep_seqs.push_back(static_cast<int>(i));
		}
	if (keep_seqs.size() == nseq)
		return this;
    // 2015-12-03: if resulting alignment has too few seqs, try to add some back
    if (keep_seqs.size() < 3 && nseq >= 3) {
        for (intptr_t i = 0; i < nseq && keep_seqs.size() < 3; i++) {
            if (isGapOnlySeq(i)) {
                keep_seqs.push_back(static_cast<int>(i));
            }
        }
    }
	Alignment *aln = new Alignment;
	aln->extractSubAlignment(this, keep_seqs, 0);
	return aln;
}

void Alignment::checkGappySeq(bool force_error) {
    intptr_t nseq      = getNSeq();
    int      wrong_seq = 0;
    for (intptr_t i = 0; i < nseq; i++) {
        if (isGapOnlySeq(i)) {
            std::stringstream complaint;
            complaint << "Sequence " << getSeqName(i)
                      << " (" << (i+1) << "th sequence in alignment)"
                      << " contains only gaps or missing data";
            outWarning(complaint.str());
            ++wrong_seq;
        }
    }
    return;
}

void Alignment::readAlignmentFile(InputType intype, const char* filename,
                                  const char* requested_sequence_type) {
    try {
        if (intype == InputType::IN_NEXUS) {
            std::cout << "Nexus format detected" << endl;
            readNexus(filename);
        } else if (intype == InputType::IN_FASTA) {
            std::cout << "Fasta format detected" << endl;
            readFasta(filename, requested_sequence_type);
        } else if (intype == InputType::IN_PHYLIP) {
            std::cout << "Phylip format detected" << endl;
            if (Params::getInstance().phylip_sequential_format)
                readPhylipSequential(filename, requested_sequence_type);
            else
                readPhylip(filename, requested_sequence_type);
        } else if (intype == InputType::IN_COUNTS) {
            std::cout << "Counts format (PoMo) detected" << endl;
            readCountsFormat(filename, requested_sequence_type);
        } else if (intype == InputType::IN_CLUSTAL) {
            std::cout << "Clustal format detected" << endl;
            readClustal(filename, requested_sequence_type);
        } else if (intype == InputType::IN_MSF) {
            std::cout << "MSF format detected" << endl;
            readMSF(filename, requested_sequence_type);
        } else {
            outError("Unknown sequence format,"
                     " please use PHYLIP, FASTA, CLUSTAL, MSF, or NEXUS format");
        }
    } catch (ios::failure) {
        outError(ERR_READ_INPUT);
    } catch (const char *str) {
        outError(str);
    } catch (string& str) {
        outError(str);
    }
}

Alignment::Alignment(const char *filename, 
                     const char *requested_sequence_type,
                     InputType &intype, const string& model) 
                     : vector<Pattern>() {
    name = "Noname";
    this->model_name = model;
    if (requested_sequence_type!=nullptr) {
        this->sequence_type = requested_sequence_type;
    }
    aln_file = filename;
    num_states = 0;
    frac_const_sites = 0.0;
    frac_invariant_sites = 0.0;
    seq_type = SeqType::SEQ_UNKNOWN;
    STATE_UNKNOWN = 126;
    pars_lower_bound = nullptr;
    double readStart = getRealTime();
    std::cout << "Reading alignment file " << filename << " ... ";
    intype = detectInputFile(filename);

    readAlignmentFile(intype, filename, requested_sequence_type);

    if (verbose_mode >= VerboseMode::VB_MED) {
        std::cout << "Time to read input file was "
                  << (getRealTime() - readStart) << " sec." << endl;
    }
    if (getNSeq() < 3)
    {
        outError("Alignment must have at least 3 sequences");
    }
    double constCountStart = getRealTime();
    countConstSite();
    if (verbose_mode >= VerboseMode::VB_MED) {
        std::cout << "Time to count constant sites was "
                  << (getRealTime() - constCountStart) << " sec." << endl;
    }
    if (Params::getInstance().compute_seq_composition)
    {
        auto singleton_count = num_variant_sites - num_informative_sites;
        auto constant_sites = (int)floor(frac_const_sites * getNSite() + .5);
        std::cout << "Alignment has " 
                  << getNSeq() << " sequences with " 
                  << getNSite() << " columns, " 
                  << getNPattern() << " distinct patterns" << endl
                  << num_informative_sites << " parsimony-informative, "
                  << singleton_count << " singleton sites, "
                  << constant_sites << " constant sites" << endl;
    }
    //buildSeqStates();
    checkSeqName();
    // OBSOLETE: identical sequences are handled later
//	checkIdenticalSeq();
    //cout << "Number of character states is " << num_states << endl;
    //cout << "Number of patterns = " << size() << endl;
    //cout << "Fraction of constant sites: " << frac_const_sites << endl;

}

Alignment::Alignment(NxsDataBlock *data_block, char *sequence_type,
                     const string& model) : vector<Pattern>() {
    name = "Noname";
    this->model_name = model;
    if (sequence_type) {
        this->sequence_type = sequence_type;
    }
    num_states = 0;
    frac_const_sites = 0.0;
    frac_invariant_sites = 0.0;
    seq_type = SeqType::SEQ_UNKNOWN;
    STATE_UNKNOWN = 126;
    pars_lower_bound = nullptr;
    
    extractDataBlock(data_block);

    if (verbose_mode >= VerboseMode::VB_DEBUG) {
        data_block->Report(cout);
    }
    if (getNSeq() < 3) {
        outError("Alignment must have at least 3 sequences");
    }
    countConstSite();
    
    if (Params::getInstance().compute_seq_composition)
        cout << "Alignment has " << getNSeq() << " sequences with " << getNSite()
             << " columns, " << getNPattern() << " distinct patterns" << endl
             << num_informative_sites << " parsimony-informative, "
             << num_variant_sites-num_informative_sites << " singleton sites, "
             << (int)(frac_const_sites*getNSite()) << " constant sites" << endl;
    //buildSeqStates();
    checkSeqName();
    // OBSOLETE: identical sequences are handled later
    //    checkIdenticalSeq();
    //cout << "Number of character states is " << num_states << endl;
    //cout << "Number of patterns = " << size() << endl;
    //cout << "Fraction of constant sites: " << frac_const_sites << endl;
    
}
bool Alignment::isStopCodon(int state) {
    // 2017-05-27: all stop codon removed from Markov process
    return false;

	if (seq_type != SeqType::SEQ_CODON || state >= num_states) {
        return false;
    }
    ASSERT(!genetic_code.empty());
	return (genetic_code[state] == '*');
}

int Alignment::getNumNonstopCodons() {
    if (seq_type != SeqType::SEQ_CODON) {
        return num_states;
    }
	ASSERT(!genetic_code.empty());
	int c = 0;
    for (auto ch: genetic_code) {
        c += (ch == '*') ? 0 : 1;
    }
	return c;
}

bool Alignment::isStandardGeneticCode() {
    if (seq_type != SeqType::SEQ_CODON) {
        return false;
    }
	return (genetic_code == genetic_code1 || 
            genetic_code == genetic_code11);
}

/*
void Alignment::buildSeqStates(vector<vector<int> > &seq_states, bool add_unobs_const) {
	vector<StateType> unobs_const;
    if (add_unobs_const) {
        unobs_const.resize(num_states);
        for (StateType state = 0; state < num_states; state++)
            unobs_const[state] = state;
    }
	seq_states.clear();
	seq_states.resize(getNSeq());
	for (intptr_t seq = 0; seq < getNSeq(); seq++) {
		BoolVector has_state(STATE_UNKNOWN+1, false);
		for (int site = 0; site < getNPattern(); site++)
			has_state[at(site)[seq]] = true;
        for (StateType it : unobs_const)
			has_state[it] = true;
        seq_states[seq].clear();
		for (int state = 0; state < STATE_UNKNOWN; state++)
			if (has_state[state])
				seq_states[seq].push_back(state);
	}
}
*/

int Alignment::readNexus(const char *filename) {
    NxsTaxaBlock *taxa_block;
    NxsAssumptionsBlock *assumptions_block;
    NxsDataBlock *data_block = NULL;
    NxsTreesBlock *trees_block = NULL;
    NxsCharactersBlock *char_block = NULL;

    taxa_block = new NxsTaxaBlock();
    assumptions_block = new NxsAssumptionsBlock(taxa_block);
    data_block = new NxsDataBlock(taxa_block, assumptions_block);
    char_block = new NxsCharactersBlock(taxa_block, assumptions_block);
    trees_block = new TreesBlock(taxa_block);

    MyReader nexus(filename);

    nexus.Add(taxa_block);
    nexus.Add(assumptions_block);
    nexus.Add(data_block);
	nexus.Add(char_block);
    nexus.Add(trees_block);

    MyToken token(nexus.inf);
    nexus.Execute(token);

    if (data_block->GetNTax() && char_block->GetNTax()) {
        outError("I am confused since both"
                 " DATA and CHARACTERS blocks were specified");
        return 0;
    }

    if (data_block->GetNTax() == 0 && char_block->GetNTax() == 0) {
        outError("No DATA or CHARACTERS blocks found");
        return 0;
    }

    if (char_block->GetNTax() > 0) {
        extractDataBlock(char_block);
        if (verbose_mode >= VerboseMode::VB_DEBUG)
            char_block->Report(cout);
    } else {
        extractDataBlock(data_block);
        if (verbose_mode >= VerboseMode::VB_DEBUG)
            data_block->Report(cout);
    }

    delete trees_block;
    delete char_block;
    delete data_block;
    delete assumptions_block;
    delete taxa_block;
    return 1;
}

void Alignment::computeUnknownState() {
    switch (seq_type) {
    case SeqType::SEQ_DNA: STATE_UNKNOWN = 18; break;
    case SeqType::SEQ_PROTEIN: STATE_UNKNOWN = 23; break;
    case SeqType::SEQ_POMO: {
        if (pomo_sampling_method == SamplingType::SAMPLING_SAMPLED) {
            STATE_UNKNOWN = num_states;
        }
        else STATE_UNKNOWN = 0xffffffff; // only dummy, will be initialized later
        break;
    }
    default: STATE_UNKNOWN = num_states; break;
    }
}

int getDataBlockMorphStates(NxsCharactersBlock *data_block) {
    int nseq = data_block->GetNTax();
    int nsite = data_block->GetNCharTotal();
    int seq, site;
    char ch;
    int nstates = 0;

    for (seq = 0; seq < nseq; seq++)
        for (site = 0; site < nsite; site++) {
            int nstate = data_block->GetNumStates(seq, site);
            if (nstate == 0)
                continue;
            if (nstate == 1) {
                ch = data_block->GetState(seq, site, 0);
                if (!isalnum(ch)) continue;
                if (ch >= '0' && ch <= '9')
                    ch = ch - '0' + 1;
                else if (ch >= 'A' && ch <= 'Z')
                    ch = ch - 'A' + 11;
                else
                    outError(data_block->GetTaxonLabel(seq) +
                             " has invalid single state " + ch +
                             " at site " + convertIntToString(site+1));
                if (ch > nstates) nstates = ch;
                continue;
            }
            //cout << "NOTE: " << data_block->GetTaxonLabel(seq)
            //     << " has ambiguous state at site " << site+1
            //     << " which is treated as unknown" << endl;
        }
    return nstates;
}

void Alignment::determineSeqTypeStatesAndSymbols
        (NxsCharactersBlock::DataTypesEnum data_type, 
         NxsCharactersBlock *data_block, char*& symbols) {
    if (data_type == NxsCharactersBlock::continuous) {
        outError("Continuous characters not supported");
    } else if (data_type == NxsCharactersBlock::dna ||
               data_type == NxsCharactersBlock::rna ||
               data_type == NxsCharactersBlock::nucleotide)
    {
        num_states = 4;
        if (data_type == NxsCharactersBlock::rna) {
            symbols = symbols_rna;
        }
        else {
            symbols = symbols_dna;
        }
        seq_type = SeqType::SEQ_DNA;
    } else if (data_type == NxsCharactersBlock::protein) {
        num_states = 20;
        symbols = symbols_protein;
        seq_type = SeqType::SEQ_PROTEIN;
    } else {
    	// standard morphological character
//        num_states = data_block->GetMaxObsNumStates();
        num_states = getDataBlockMorphStates(data_block);
        if (num_states > 32) {
        	outError("Number of states can not exceed 32");
        }
        if (num_states < 2) {
        	outError("Number of states can not be below 2");
        }
        if (num_states == 2) {
        	seq_type = SeqType::SEQ_BINARY;
        }
        else {
    		seq_type = SeqType::SEQ_MORPH;
        }
        symbols = symbols_morph;
    }
}

void Alignment::extractDataBlock(NxsCharactersBlock *data_block) {
    char char_to_state[NUM_CHAR];
    char state_to_char[NUM_CHAR];
    
    extractStateMatricesFromDataBlock(data_block, char_to_state, state_to_char);
    extractSequenceNamesFromDataBlock(data_block);

    auto data_type = (NxsCharactersBlock::DataTypesEnum)data_block->GetDataType();
    int  nseq      = data_block->GetNTax();
    int  nsite     = data_block->GetNCharTotal();

    site_pattern.resize(nsite, -1);

    int  num_gaps_only = 0;
    for (int site = 0; site < nsite; site++) {
        Pattern pat;
        for (int seq = 0; seq < nseq; seq++) {
            int nstate = data_block->GetNumStates(seq, site);
            if (nstate == 0) {
                pat.push_back(STATE_UNKNOWN);
            }
            else if (nstate == 1) {
                pat.push_back(char_to_state[(int)data_block->GetState(seq, site, 0)]);
            } else if (data_type == NxsCharactersBlock::dna ||
                       data_type == NxsCharactersBlock::rna ||
                       data_type == NxsCharactersBlock::nucleotide) {
                // 2018-06-07: correctly interpret ambiguous nucleotide
                char pat_ch = 0;
                for (int state = 0; state < nstate; state++) {
                    pat_ch |= (1 << char_to_state[(int)data_block->GetState(seq, site, state)]);
                }
                pat_ch += 3;
                pat.push_back(pat_ch);
            } else {
                // other ambiguous characters are treated as unknown
                stringstream str;
                str << "Sequence " << seq_names[seq] << " site " << site+1 << ": {";
                for (int state = 0; state < nstate; state++) {
                    str << data_block->GetState(seq, site, state);
                }
                str << "} treated as unknown character";
                outWarning(str.str());
                pat.push_back(STATE_UNKNOWN);
            }
        }
        num_gaps_only += addPattern(pat, site);
    }
    if ( 0 < num_gaps_only ) {
        cout << "WARNING: " << num_gaps_only
             << " sites contain only gaps or ambiguous characters." << endl;
    }
    if (verbose_mode >= VerboseMode::VB_MAX) {
        for (int site = 0; site < size(); site++) {
            for (int seq = 0; seq < nseq; seq++) {
                cout << state_to_char[(int)(*this)[site][seq]];
            }
            cout << "  " << (*this)[site].frequency << endl;
        }
    }
}

void Alignment::extractStateMatricesFromDataBlock
        (NxsCharactersBlock *data_block,
         char* char_to_state, char* state_to_char) {
    if (!data_block->GetMatrix()) {
        outError("MATRIX command undeclared or invalid");
    }
    
    auto  data_type = (NxsCharactersBlock::DataTypesEnum)data_block->GetDataType();
    char* symbols   = nullptr;
    determineSeqTypeStatesAndSymbols(data_type, data_block, symbols);

    computeUnknownState();
    memset(char_to_state, STATE_UNKNOWN, NUM_CHAR);
    memset(state_to_char, '?', NUM_CHAR);
    for (int i = 0; i < strlen(symbols); i++) {
        char_to_state[(int)symbols[i]] = i;
        state_to_char[i] = symbols[i];
    }
    state_to_char[(int)STATE_UNKNOWN] = '-';
}

void Alignment::extractSequenceNamesFromDataBlock
        (NxsCharactersBlock *data_block) {
    int  nseq  = data_block->GetNTax();
    if (data_block->taxa->GetNumTaxonLabels() == 0) {
        outError("MATRIX not found, make sure nexus command"
                 " before MATRIX ends with semi-colon (;)");
    }
    if (data_block->taxa->GetNumTaxonLabels() != nseq) {
        outError("ntax is different from number of matrix rows");
    }
    for (int seq = 0; seq < nseq; seq++) {
        seq_names.push_back(data_block->GetTaxonLabel(seq));
    }
}

/**
	determine if the pattern is constant. update the is_const variable.
*/
void Alignment::computeConst(Pattern &pat) {
    pat.countAppearances   (this);
    pat.setInformativeFlags(this);
}

void Alignment::printSiteInfo(ostream &out, int part_id) {
    size_t nsite = getNSite();
    for (size_t site = 0; site != nsite; site++) {
        Pattern ptn = getPattern(site);
        if (part_id >= 0)
            out << part_id << "\t";
        out << site+1 << "\t";
        if (ptn.isInformative())
            out << "I";
        else if (ptn.isConst()) {
            if (ptn.const_char == STATE_UNKNOWN)
                out << "-";
            else if (ptn.const_char < num_states)
                out << "C";
            else
                out << "c";
        } else
            out << "U";
        out << endl;
    }
}

void Alignment::printSiteInfoHeader(ostream &out, const char* filename, bool partition) {
    out << "# Alignment site statistics" << endl
        << "# This file can be read in MS Excel or in R with command:" << endl
        << "#   tab=read.table('" <<  filename << "',header=TRUE)" << endl
        << "# Columns are tab-separated with following meaning:" << endl;
    if (partition)
        out << "#   Part:   Partition ID" << endl
            << "#   Site:   Site ID within partition (starting from 1 for each partition)" << endl;
    else
        out << "#   Site:   Site ID" << endl;

    out << "#   Stat:   Statistic, I=informative, C=constant, c=constant+ambiguous," << endl
        << "#           U=Uninformative but not constant, -=all-gaps" << endl;
    if (partition)
        out << "Part\t";
    out << "Site\tStat" << endl;
}

void Alignment::printSiteInfo(const char* filename) {
    try {
        ofstream out(filename);
        printSiteInfoHeader(out, filename);
        printSiteInfo(out, -1);
        out.close();
    } catch (...) {
        outError(ERR_WRITE_OUTPUT, filename);
    }
}

bool Alignment::addPatternLazy(Pattern &pat, intptr_t site,
                               int freq, bool& gaps_only) {
    //Returns true if the pattern was actually added, false
    //if it was identified as a duplicate (and handled by
    //increasing the frequency of an existing pattern)
    // check if pattern contains only gaps
    gaps_only = pat.isAllGaps(STATE_UNKNOWN);
    if (gaps_only) {
        if (verbose_mode >= VerboseMode::VB_DEBUG) {
            cout << "Site " << site << " contains only"
                 << " gaps or ambiguous characters" << endl;
        }
    }
    PatternIntMap::iterator pat_it = pattern_index.find(pat);
    if (pat_it == pattern_index.end()) { // not found
        pat.frequency = freq;
        //We don't do computeConst(pat); here, that's why
        //there's a "Lazy" in this member function's name!
        //We do that in addPattern...
        push_back(pat);
        pattern_index[back()] = static_cast<int>(size())-1;
        site_pattern[site]    = static_cast<int>(size())-1;
        return true;
    } else {
        int index = pat_it->second;
        at(index).frequency += freq;
        site_pattern[site] = index;
        return false;
    }
}

bool Alignment::addPattern(Pattern &pat, int site, int freq) {
    bool gaps_only = false;
    if (addPatternLazy(pat, site, freq, gaps_only)) {
        computeConst(back());
    }
    return gaps_only;
}

void Alignment::updatePatterns(intptr_t oldPatternCount) {
    intptr_t patternCount = size();
    
    #ifdef _OPENMP
    #pragma omp parallel for
    #endif
    for (intptr_t patIndex = oldPatternCount;
         patIndex < patternCount; ++patIndex ) {
        computeConst(at(patIndex));
    }
}

void Alignment::addConstPatterns(char *freq_const_patterns) {
	IntVector vec;
	convert_int_vec(freq_const_patterns, vec);
	if (vec.size() != num_states) {
		outError("Const pattern frequency vector" 
                 " has different number of states: ", freq_const_patterns);
    }
    intptr_t nsite      = getNSite();
    intptr_t orig_nsite = getNSite();
    intptr_t vec_size   = static_cast<intptr_t>(vec.size());

	for (intptr_t i = 0; i < vec_size; i++) {
		nsite += vec[i];
        if (vec[i] < 0) {
            outError("Const pattern frequency must be non-negative");
        }
	}
    site_pattern.resize(nsite, -1);
	size_t nseq = getNSeq();
	nsite = orig_nsite;
    intptr_t oldPatternCount = size();
    for (int i = 0; i < static_cast<int>(vec.size()); i++) {
        if (vec[i] > 0) {
            Pattern pat;
            pat.resize(nseq, i);
            //if (pattern_index.find(pat) != pattern_index.end()) {
            //  outWarning("Constant pattern of all "
            //             + convertStateBackStr(i) + " already exists");
            //}
            for (int j = 0; j < vec[i]; j++) {
                bool gaps_only;
                addPatternLazy(pat, nsite++, 1, gaps_only);
            }
        }
    }
    updatePatterns(oldPatternCount);
    countConstSite();
}

void Alignment::orderPatternByNumChars(int pat_type) {
    intptr_t nptn       = getNPattern();
    const int UINT_BITS = sizeof(UINT)*8;
    if (pat_type == PAT_INFORMATIVE) {
        num_parsimony_sites = num_informative_sites;
    } else {
        num_parsimony_sites = num_variant_sites;
    }

    size_t frequency_total = 0;
    {
        intptr_t* num_chars      = new intptr_t[nptn];
        intptr_t* ptn_order      = new intptr_t[nptn];
        #ifdef _OPENMP
        #pragma omp parallel for
        #endif
        for (intptr_t ptn = 0; ptn < nptn; ++ptn) {
            num_chars[ptn] =  - at(ptn).num_chars
                              + (at(ptn).isInvariant())*1024;
            ptn_order[ptn] = ptn;
        }
        quicksort(num_chars, 0, static_cast<int>(nptn-1), ptn_order);
        delete [] num_chars;
        for (intptr_t ptn = 0; ptn < nptn; ++ptn) {
            Pattern& pat = at(ptn_order[ptn]);
            if (pat.isInvariant()) {
                nptn = ptn;
            }
            else if (pat_type == PAT_INFORMATIVE && !pat.isInformative() ) {
                nptn = ptn;
            }
        }
        ordered_pattern.clear();
        ordered_pattern.resize(nptn);
        #ifdef _OPENMP
        #pragma omp parallel for reduction(+:frequency_total)
        #endif
        for (intptr_t ptn = 0; ptn < nptn; ++ptn) {
            ordered_pattern[ptn] = at(ptn_order[ptn]);
            frequency_total += ordered_pattern[ptn].frequency;
        }
        delete [] ptn_order;
    }

    size_t maxi      = (frequency_total+UINT_BITS-1)/UINT_BITS;
    delete[] pars_lower_bound;
    pars_lower_bound = nullptr;
    pars_lower_bound = new UINT[maxi+1];
    memset(pars_lower_bound, 0, (maxi+1)*sizeof(UINT));

    int site = 0;
    size_t i = 0;
    UINT sum = 0;
    for (intptr_t ptn = 0; ptn < nptn; ptn++) {
        Pattern& pat = ordered_pattern[ptn];
        for (int j = pat.frequency; 0 < j; --j, site++) {
            if (site == UINT_BITS) {
                sum += pars_lower_bound[i];
                ++i;
                site = 0;
            }
            ASSERT(i<maxi);
            pars_lower_bound[i] += pat.num_chars - 1;
        }
    }
    sum += pars_lower_bound[i];

    // now transform lower_bound
    for (int j = 0; j <= i; j++) {
        UINT newsum = sum - pars_lower_bound[j];
        pars_lower_bound[j] = sum;
        sum = newsum;
    }

    if (verbose_mode >= VerboseMode::VB_MAX) {
        //for (ptn = 0; ptn < nptn; ptn++)
        //    cout << at(ptn_order[ptn]).num_chars << " ";
        for (int j = 0; j <= i; j++) {
            cout << pars_lower_bound[j] << " ";
        }
        cout << endl << sum << endl;
    }

    // fill up to vectorclass with dummy pattern
    intptr_t maxnptn = get_safe_upper_limit_float(num_parsimony_sites);
    intptr_t nseq    = getNSeq();
    for (intptr_t ptn = nptn; ptn<maxnptn; ++ptn) {
        Pattern pat;
        pat.resize(nseq, STATE_UNKNOWN);
        pat.frequency = 0;
        ordered_pattern.emplace_back(pat);
    }
}

void Alignment::ungroupSitePattern()
{
	vector<Pattern> stored_pat = (*this);
	clear();
    //Note: Doesn't scale to > 2 billion sites
    //(because site_pattern an IntVector, not an Int64Vector).
	for (int i = 0; i < static_cast<int>(getNSite()); ++i) {
		Pattern pat = stored_pat[getPatternID(i)];
		pat.frequency = 1;
		push_back(pat);
		site_pattern[i] = i;
	}
	pattern_index.clear();
}

void Alignment::regroupSitePattern(int groups, IntVector& site_group)
{
	vector<Pattern> stored_pat = (*this);
	IntVector stored_site_pattern = site_pattern;
	clear();
	site_pattern.clear();
	site_pattern.resize(stored_site_pattern.size(), -1);
	size_t count = 0;
	for (int g = 0; g < groups; g++) {
		pattern_index.clear();
        for (int i = 0; i < static_cast<int>(site_group.size()); ++i) {
            if (site_group[i] == g) {
                count++;
                Pattern pat = stored_pat[stored_site_pattern[i]];
                addPattern(pat, i);
            }
        }
	}
	ASSERT(count == stored_site_pattern.size());
	count = 0;
	for (iterator it = begin(); it != end(); ++it)
		count += it->frequency;
	ASSERT(count == getNSite());
	pattern_index.clear();
	//printPhylip("/dev/stdout");
}

Alignment::CharacterCountsByType::CharacterCountsByType()
    : num_nuc(0),   num_ungap(0), num_bin(0)
    , num_alpha(0), num_digit(0) {
}

void Alignment::CharacterCountsByType::countCharactersByType
        (StrVector& sequences) {
    //Visual Studio's open mp implementation won't allow
    //class members to be used in a data-sharing clause!
    intptr_t sequenceCount = sequences.size();
    size_t local_num_nuc   = num_nuc;
    size_t local_num_ungap = num_ungap;
    size_t local_num_bin   = num_bin;
    size_t local_num_alpha = num_alpha;
    size_t local_num_digit = num_digit;

#ifdef _OPENMP
#pragma omp parallel for \
    reduction(+:local_num_nuc,local_num_ungap,local_num_bin,local_num_alpha,local_num_digit)
#endif
    for (intptr_t seqNum = 0; seqNum < sequenceCount; ++seqNum) {
        auto start = sequences.at(seqNum).data();
        auto stop  = start + sequences.at(seqNum).size();
        for (auto i = start; i!=stop; ++i) {
            if ((*i) == 'A' || (*i) == 'C' || (*i) == 'G' ||
                (*i) == 'T' || (*i) == 'U') {
                ++local_num_nuc;
                ++local_num_ungap;
                ++local_num_alpha;
                continue;
            }
            if ((*i)=='?' || (*i)=='-' || (*i) == '.' ) {
                continue;
            }
            if (*i != 'N' && *i != 'X' &&  (*i) != '~') {
                local_num_ungap++;
                if (isdigit(*i)) {
                    local_num_digit++;
                    if ((*i) == '0' || (*i) == '1') {
                        local_num_bin++;
                    }
                }
            }
            if (isalpha(*i)) {
                local_num_alpha++;
            }
        }
    }
    //Copy back from the local variables that Visual Studio
    //insisted upon(!).
    num_nuc   = local_num_nuc;
    num_ungap = local_num_ungap;
    num_bin   = local_num_bin;
    num_alpha = local_num_alpha;
    num_digit = local_num_digit;
}
/**
	detect the data type of the input sequences
	@param sequences vector of strings
	@return the data type of the input sequences
*/
SeqType Alignment::detectSequenceType(StrVector &sequences) {
    double   detectStart = getRealTime();
    Alignment::CharacterCountsByType counts;
    counts.countCharactersByType(sequences);

    if (verbose_mode >= VerboseMode::VB_MED) {
        cout << "Sequence Type detection took "
             << (getRealTime()-detectStart) << " seconds." << endl;
    }
    if (counts.num_ungap==0) {
        return SeqType::SEQ_UNKNOWN;
    }
    if (((double)counts.num_nuc) / counts.num_ungap > 0.9) {
        return SeqType::SEQ_DNA;
    }
    if (((double)counts.num_bin) / counts.num_ungap > 0.9) {
        return SeqType::SEQ_BINARY;
    }
    if (((double)counts.num_alpha) / counts.num_ungap > 0.9) {
        return SeqType::SEQ_PROTEIN;
    }
    if (((double)(counts.num_alpha+counts.num_digit)) / counts.num_ungap > 0.9) {
        return SeqType::SEQ_MORPH;
    }
    return SeqType::SEQ_UNKNOWN;
}

void Alignment::buildStateMap(char *map, SeqType seq_type) {
    memset(map, STATE_INVALID, NUM_CHAR);
    ASSERT(STATE_UNKNOWN < 126);
    map[(unsigned char)'?'] = STATE_UNKNOWN;
    map[(unsigned char)'-'] = STATE_UNKNOWN;
    map[(unsigned char)'~'] = STATE_UNKNOWN;
    map[(unsigned char)'.'] = STATE_UNKNOWN;
    int len;
    switch (seq_type) {
    case SeqType::SEQ_BINARY:
        map[(unsigned char)'0'] = 0;
        map[(unsigned char)'1'] = 1;
        return;
    case SeqType::SEQ_DNA: // DNA
	case SeqType::SEQ_CODON:
        map[(unsigned char)'A'] = 0;
        map[(unsigned char)'C'] = 1;
        map[(unsigned char)'G'] = 2;
        map[(unsigned char)'T'] = 3;
        map[(unsigned char)'U'] = 3;
        map[(unsigned char)'R'] = 1+4+3; // A or G, Purine
        map[(unsigned char)'Y'] = 2+8+3; // C or T, Pyrimidine
        map[(unsigned char)'N'] = STATE_UNKNOWN;
        map[(unsigned char)'X'] = STATE_UNKNOWN;
        map[(unsigned char)'W'] = 1+8+3; // A or T, Weak
        map[(unsigned char)'S'] = 2+4+3; // G or C, Strong
        map[(unsigned char)'M'] = 1+2+3; // A or C, Amino
        map[(unsigned char)'K'] = 4+8+3; // G or T, Keto
        map[(unsigned char)'B'] = 2+4+8+3; // C or G or T
        map[(unsigned char)'H'] = 1+2+8+3; // A or C or T
        map[(unsigned char)'D'] = 1+4+8+3; // A or G or T
        map[(unsigned char)'V'] = 1+2+4+3; // A or G or C
        return;
    case SeqType::SEQ_PROTEIN: // Protein
        for (int i = 0; i < 20; i++)
            map[(int)symbols_protein[i]] = i;
        map[(int)symbols_protein[20]] = STATE_UNKNOWN;
//		map[(unsigned char)'B'] = 4+8+19; // N or D
//		map[(unsigned char)'Z'] = 32+64+19; // Q or E
        map[(unsigned char)'B'] = 20; // N or D
        map[(unsigned char)'Z'] = 21; // Q or E
        map[(unsigned char)'J'] = 22; // I or L
        map[(unsigned char)'*'] = STATE_UNKNOWN; // stop codon
        map[(unsigned char)'U'] = STATE_UNKNOWN; // 21st amino acid
        map[(unsigned char)'O'] = STATE_UNKNOWN; // 22nd amino acid

        return;
    case SeqType::SEQ_MULTISTATE:
        for (int i = 0; i <= static_cast<int>(STATE_UNKNOWN); i++)
            map[i] = i;
        return;
    case SeqType::SEQ_MORPH: // Protein
    	len = static_cast<int>(strlen(symbols_morph));
        for (int i = 0; i < len; i++)
            map[(int)symbols_morph[i]] = i;
        return;
    default:
        return;
    }
}


/**
	convert a raw characer state into ID, indexed from 0
	@param state input raw state
	@param seq_type data type (SEQ_DNA, etc.)
	@return state ID
*/
StateType Alignment::convertState(char state, SeqType seq_type) const {
    if (state == '?' || state == '-' || state == '.' || state == '~') {
        return STATE_UNKNOWN;
    }
    switch (seq_type) {
        case SeqType::SEQ_BINARY:
            return convertBinaryState(state);
        case SeqType::SEQ_DNA: // DNA
            return convertDNAState(state);
        case SeqType::SEQ_PROTEIN: // Protein
            return convertProteinState(state);
        case SeqType::SEQ_MORPH: // Standard morphological character
            return convertMorphologicalState(state);
        default:
            return STATE_INVALID;
    }
}

StateType Alignment::convertBinaryState(char state) const {
    switch (state) {
        case '0':
            return 0;
        case '1':
            return 1;
        default:
            return STATE_INVALID;
    }
}

namespace {
    const struct { char ch; StateType state_type; } dna_map[] = {
        { 'A', 0 },
        { 'C', 1 },
        { 'G', 2 },
        { 'T', 3 },
        { 'U', 3 },
        { 'R', 1+4+3},   // A or G, Purine
        { 'Y', 2+8+3},   // C or T, Pyrimidine
        { 'W', 1+8+3},   // A or T, Weak
        { 'S', 2+4+3},   // G or C, Strong
        { 'M', 1+2+3},   // A or C, Amino
        { 'K', 4+8+3},   // G or T, Keto
        { 'B', 2+4+8+3}, // C or G or T
        { 'H', 1+2+8+3}, // A or C or T
        { 'D', 1+4+8+3}, // A or G or T
        { 'V', 1+2+4+3}, // A or G or C
    };
}

StateType Alignment::convertDNAState(char state) const {
    if (state=='O' || state=='N' || state=='X') {
        return STATE_UNKNOWN;
    }
    for (auto map_entry: dna_map) {
        if (state==map_entry.ch) {
            return map_entry.state_type;
        }
    }
    return STATE_INVALID; // unrecognize character
}

StateType Alignment::convertProteinState(char state) const {
    //if (state == 'B') return 4+8+19;
    //if (state == 'Z') return 32+64+19;
    if (state == 'B') return 20;
    if (state == 'Z') return 21;
    if (state == 'J') return 22;
    if (state == '*') return STATE_UNKNOWN; // stop codon
    if (state == 'U') return STATE_UNKNOWN; // 21st amino-acid
    if (state == 'O') return STATE_UNKNOWN; // 22nd amino-acid
    const char* loc = strchr(symbols_protein, state);
    if (loc==nullptr) {
        return STATE_INVALID; // unrecognized character
    }
    state = static_cast<char>(loc - symbols_protein);
    if (state < 20) {
        return state;
    }
    return STATE_UNKNOWN;
}

StateType Alignment::convertMorphologicalState(char state) const {
    const char* loc = strchr(symbols_morph, state);
    if (loc==nullptr) {
        return STATE_INVALID; // unrecognize character
    }
    state = static_cast<char>(loc - symbols_morph);
	return state;
}

// TODO: state should be int
StateType Alignment::convertState(char state) {
	return convertState(state, seq_type);
}

// TODO: state should be int
char Alignment::convertStateBack(char state) const {
    if (state == STATE_UNKNOWN) {
        return '-';
    }
    if (state == STATE_INVALID) {
        return '?';
    }
    switch (seq_type) {
        case SeqType::SEQ_BINARY:
            return convertBinaryStateBack(state);
        case SeqType::SEQ_DNA: // DNA
            return convertDNAStateBack(state);
        case SeqType::SEQ_PROTEIN: // Protein
            return convertProteinStateBack(state);
        case SeqType::SEQ_MORPH:
            return convertMorphologicalStateBack(state);
        default:
            // unknown
            return '*';
    }
}

char Alignment::convertBinaryStateBack(char state) const {
    switch (state) {
        case 0:
            return '0';
        case 1:
            return '1';
        default:
            return '?'; //Formerly, was returning STATE_INVALID;
    }
}

char Alignment::convertDNAStateBack(char state) const {
    //Note: Because T appears before U in dna_map,
    //      a state of 3 comes out as 'T', not 'U'.
    for (auto map_entry: dna_map) {
        if (state==map_entry.state_type) {
            return map_entry.ch;
        }
    }
    return '?'; // unrecognized character
}

char Alignment::convertProteinStateBack(char state) const {
    if (state < 20) {
        return symbols_protein[(int)state];
    } else if (state == 20) {
        return 'B';
    } else if (state == 21) {
        return 'Z';
    } else if (state == 22) {
        return 'J';
    }
    // else if (state == 4+8+19) return 'B';
    // else if (state == 32+64+19) return 'Z';
    else {
        return '-';
    }
}

char Alignment::convertMorphologicalStateBack(char state) const {
    // morphological state
    if (state < strlen(symbols_morph)) {
        return symbols_morph[(int)state];
    } else {
        return '-';
    }
}

string Alignment::convertStateBackStr(StateType state) const {
	string str;
    if (seq_type == SeqType::SEQ_POMO) {
        return string("POMO") + convertIntToString(state);
    }
    if (seq_type == SeqType::SEQ_MULTISTATE) {
        return " " + convertIntToString(state);
    }
	if (seq_type == SeqType::SEQ_CODON) {
        // codon data
        if (static_cast<int>(state) >= num_states) {
            return "???";
        }
        assert(!codon_table.empty());
        state = codon_table[(int)state];
        str = symbols_dna[state/16];
        str += symbols_dna[(state%16)/4];
        str += symbols_dna[state%4];
        return str;
	}
    // all other data types
    str = convertStateBack(state);
	return str;
}

/*
void Alignment::convertStateStr(string &str, SeqType seq_type) {
    for (string::iterator it = str.begin(); it != str.end(); it++)
        (*it) = convertState(*it, seq_type);
}
*/
 
void Alignment::initCodon(const char *gene_code_id, bool nt2aa) {
    // build index from 64 codons to non-stop codons
	if (strlen(gene_code_id) > 0) {
    	int transl_table = 1;
		try {
			transl_table = convert_int(gene_code_id);
		} catch (string &) {
			outError("Wrong genetic code ", gene_code_id);
		}
        bool code_found = false;
        if (min_translation_table <= transl_table &&
            transl_table <= max_translation_table ) {
            if (genetic_codes[transl_table]!=nullptr) {
                genetic_code = genetic_codes[transl_table];
                code_found = true;
            }
        }
        if (!code_found) {
			outError("Wrong genetic code ", gene_code_id);
        }
	} else {
		genetic_code = genetic_code1;
	}
    const int num_codons = static_cast<int>(genetic_code.length());
    ASSERT(num_codons == 64);
    
    std::set<char> proteins;
    int num_proteins = 0;
    int num_non_stop_codons = 0;
    for (int codon = 0; codon < num_codons; codon++) {
        if (genetic_code[codon] != '*') {
            if (proteins.insert(genetic_code[codon]).second) {
                ++num_proteins; // only count distinct non-stop codons
            }
            ++num_non_stop_codons;
        }
    }
    codon_table.clear();
    codon_table.resize(num_non_stop_codons,0);
    non_stop_codon.clear();
    non_stop_codon.resize(num_codons,0);
    int state = 0;
    for (int codon = 0; codon < num_codons; codon++) {
        if (genetic_code[codon] != '*') {
            codon_table[state]    = codon;
            non_stop_codon[codon] = state;
            ++state;
        } else {
            non_stop_codon[codon] = STATE_INVALID;
        }
    }
    seq_type   = nt2aa ? SeqType::SEQ_PROTEIN : SeqType::SEQ_CODON;
    num_states = nt2aa ? num_proteins         : num_non_stop_codons;
}

int getMorphStates(const StrVector &sequences) {
    char maxstate = 0;
    for (auto it = sequences.begin(); it != sequences.end(); it++) {
        for (auto pos = it->begin(); pos != it->end(); pos++) {
            if ((*pos) > maxstate && isalnum(*pos)) {
                maxstate = *pos;
            }
        }
    }
    if (maxstate >= '0' && maxstate <= '9') {
        return (maxstate - '0' + 1);
    }
    if (maxstate >= 'A' && maxstate <= 'V') {
        return (maxstate - 'A' + 11);
    }
    return 0;
}

bool Alignment::buildPattern(StrVector &sequences,
                             const char *sequence_type,
                             int nseq, int nsite) {
    codon_table.clear();
    genetic_code.clear();
    non_stop_codon.clear();
    if (nseq != seq_names.size()) {
        throw "Different number of sequences than specified";
    }
    double seqCheckStart = getRealTime();    
    checkSequenceNamesAreCorrect(nseq, nsite, seqCheckStart, sequences);

    seq_type   = detectSequenceType(sequences);
    num_states = determineNumberOfStates(seq_type, sequences, sequence_type);
    bool nt2aa = false;
    checkDataType(sequence_type, sequences, nt2aa);

    return constructPatterns(nseq, nsite, sequences, nullptr);
}

void Alignment::checkSequenceNamesAreCorrect(int nseq, int nsite, 
                                             double seqCheckStart,
                                             const StrVector &sequences ) {
    unordered_set<string> namesSeen;
    ostringstream err_str;
    /* now check that all sequence names are correct */
    for (int seq_id = 0; seq_id < nseq; seq_id ++) {
        if (seq_names[seq_id] == "")
            err_str << "Sequence number " << seq_id+1
                    << " has no names\n";
        // check that all the names are different
        if (!namesSeen.insert(seq_names[seq_id]).second) {
            err_str << "The sequence name " << seq_names[seq_id]
                    << " is duplicated\n";
        }
    }
    if (err_str.str() != "")
    {
        throw err_str.str();
    }
    if (verbose_mode >= VerboseMode::VB_MED) {
        cout.precision(6);
        cout << "Duplicate sequence name check took "
                << (getRealTime()-seqCheckStart) << " seconds." << endl;
    }
    /* now check that all sequences have the same length */
    for (int seq_id = 0; seq_id < nseq; seq_id ++) {
        if (sequences[seq_id].length() != nsite) {
            err_str << "Sequence " << seq_names[seq_id] << " contains ";
            if (sequences[seq_id].length() < nsite)
                err_str << "not enough";
            else
                err_str << "too many";
            
            err_str << " characters (" << sequences[seq_id].length() << ")\n";
        }
    }
    
    if (err_str.str() != "") {
        throw err_str.str();
    }
}

int Alignment::determineNumberOfStates(SeqType seq_type, const StrVector &sequences,
                                       const char* sequence_type) {
    switch (seq_type) {
    case SeqType::SEQ_BINARY:
        cout << "Alignment most likely contains binary sequences" << endl;
        return 2;
    case SeqType::SEQ_DNA:
        cout << "Alignment most likely contains DNA/RNA sequences" << endl;
        return 4;
    case SeqType::SEQ_PROTEIN:
        cout << "Alignment most likely contains protein sequences" << endl;
        return 20;
    case SeqType::SEQ_MORPH:
        {
            int states = getMorphStates(sequences);
            if (states < 2 || states > 32) {
                throw "Invalid number of states.";
            }
            cout << "Alignment most likely contains " << states
                << "-state morphological data" << endl;
            return states;
        }
    case SeqType::SEQ_POMO:
        throw "Counts Format pattern is built"
              " in Alignment::readCountsFormat().";
        break;
    default:
        if (!sequence_type) {
            throw "Unknown sequence type.";
        }
        break;
    }
    return 0;
}

void Alignment::checkDataType(const char* sequence_type, 
                              const StrVector &sequences, bool& nt2aa) {
    if (sequence_type == nullptr) {
        return;
    }
    if (strcmp(sequence_type,"") == 0) {
        return;
    }
    SeqType user_seq_type = getSeqType(sequence_type);
    num_states = getNumStatesForSeqType(user_seq_type, num_states);
    switch (user_seq_type) {
        case SeqType::SEQ_BINARY: /* fall-through */
        case SeqType::SEQ_DNA:
            break;
            
        case SeqType::SEQ_CODON:
            ASSERT( strncmp(sequence_type, "CODON", 5)==0 );
            if (seq_type != SeqType::SEQ_DNA) {
                outWarning("You want to use codon models"
                            " but the sequences were not detected as DNA");
            }
            cout << "Converting to codon sequences"
                    << " with genetic code " << &sequence_type[5]
                    << " ..." << endl;
            initCodon(&sequence_type[5], false);
            break;
            
        case SeqType::SEQ_MORPH:
            num_states = getMorphStates(sequences);
            if (num_states < 2 || num_states > 32) {
                throw "Invalid number of states";
            }
            break;
            
        case SeqType::SEQ_MULTISTATE:
            cout << "Multi-state data with "
                    << num_states << " alphabets" << endl;
            break;
            
        case SeqType::SEQ_PROTEIN:
            if (strncmp(sequence_type, "NT2AA", 5) == 0) {
                if (seq_type != SeqType::SEQ_DNA) {
                    outWarning("Sequence type detected as non DNA!");
                }
                initCodon(&sequence_type[5], true);
                nt2aa    = true;
                cout << "Translating to amino-acid sequences"
                        << " with genetic code " << &sequence_type[5]
                        << " ..." << endl;
            }
            break;
            
        case SeqType::SEQ_UNKNOWN:
            throw "Invalid sequence type.";
            break;
            
        default:
            std::stringstream warning;
            warning << "Your specified sequence type"
                    << " (" << getSeqTypeName(user_seq_type) << ")"
                    << " is different from the detected one"
                    << " (" << getSeqTypeName(seq_type) << ")";
            outWarning(warning.str());
            break;
    }
    seq_type = user_seq_type;
}

struct PatternInfo {
public:
    std::ostringstream errors;
    std::ostringstream warnings;
    int num_error;
    bool isAllGaps;
    PatternInfo() : num_error(0), isAllGaps(false) {}
};

class PatternInfoVector: public std::vector<PatternInfo> {
public:
    Alignment* aln;
    SeqType    seq_type;
    bool       nt2aa;
    int        num_gaps_only;
    char       char_to_state[NUM_CHAR];
    char       AA_to_state[NUM_CHAR];

    PatternInfoVector          () = delete;
    PatternInfoVector          (Alignment* for_aln, bool nt2aa);
    void loadPatterns          (int nsite, int nstep, 
                                int nseq, const StrVector& sequences,
                                progress_display_ptr progress);
    char loadCodonState        (const StrVector& sequences, 
                                int site, int seq, char state,
                                PatternInfo& info);
    void recordInvalidCharacter(const StrVector& sequences, 
                                int site, int seq, 
                                PatternInfo& info);
    int  compressPatterns      (int step, std::stringstream& err_str,
                                progress_display_ptr progress);
};

bool Alignment::constructPatterns(int nseq, int nsite,
                                  const StrVector& sequences,
                                  progress_display_ptr progress) {
    //initStateSpace(seq_type);
    // now convert to patterns
    computeUnknownState();
    bool nt2aa = strncmp(sequence_type.c_str(), "NT2AA", 5) == 0;
    int step = 1;
    if (seq_type == SeqType::SEQ_CODON || nt2aa) {
        step = 3;
        if (nsite % step != 0) {
            outError("Number of sites is not multiple of 3");
        }
    }
    site_pattern.resize(nsite/step, -1);
    clear();
    pattern_index.clear();
    singleton_parsimony_states.clear();
    total_singleton_parsimony_states = 0;
    
    //1. Construct all the patterns, in parallel (*without* trying to consolidate
    //   duplicated patterns; we'll do that later).
    resize(nsite / step);
    PatternInfoVector patternInfo(this, nt2aa);
    patternInfo.resize((nsite + (step-1)) / step);

    progress_display_ptr progress_here = nullptr;

    progressLocal(!isShowingProgressDisabled, (double) nsite, 
                  "Constructing alignment", "examined", 
                  "site", progress, progress_here);

    patternInfo.loadPatterns(nsite, step, nseq, sequences, progress);
    progressLocalDone(progress, progress_here);


    //2. Now handle warnings and errors, and compress patterns, sequentially
    progressLocal(!isShowingProgressDisabled, (double) nsite,
                  "Compressing patterns", "processed", 
                  "site", progress, progress_here);
    std::stringstream err_str;
    int w = patternInfo.compressPatterns(step, err_str, progress);
    resize(w);
    progressLocalDone(progress, progress_here);
 
    intptr_t taxon_count = getNSeq();
    singleton_parsimony_states.resize(taxon_count, 0);
    for (int p=0; p<w; ++p) {
        const Pattern& pat = at(p);
        pat.countTowardSingletonParsimonyStates(singleton_parsimony_states);
    }
    UINT total_states = 0;
    #ifdef _OPENMP
    #pragma omp parallel for reduction(+:total_states)
    #endif
    for (intptr_t taxon=0; taxon<taxon_count; ++taxon) {
        total_states += singleton_parsimony_states[taxon];
    }
    total_singleton_parsimony_states = total_states;

    if (patternInfo.num_gaps_only) {
        progressHide(progress);
        cout << "WARNING: " << patternInfo.num_gaps_only
             << " sites contain only gaps or ambiguous characters." << endl;
        progressShow(progress);
    }
    if (err_str.str() != "") {
        throw err_str.str();
    }
    return true;
}

PatternInfoVector::PatternInfoVector(Alignment* for_aln, bool is_nt2aa)
    : aln(for_aln), seq_type(for_aln->seq_type),  nt2aa(is_nt2aa)
    , num_gaps_only(0) {
    if (nt2aa) {
        aln->buildStateMap(char_to_state, SeqType::SEQ_DNA);
        aln->buildStateMap(AA_to_state,   SeqType::SEQ_PROTEIN);
    } else {
        aln->buildStateMap(char_to_state, seq_type);
    }
}

void PatternInfoVector::loadPatterns(int nsite, int step, int nseq, 
                                     const StrVector& sequences,
                                     progress_display_ptr progress) {
    #ifdef _OPENMP
    #pragma omp parallel for
    #endif
    for (int site = 0; site < nsite; site+=step) {
        PatternInfo& info = at(site/step);
        Pattern&     pat  = aln->at(site / step);
        pat.resize(nseq);
        for (int seq = 0; seq < nseq; seq++) {
            //char state = convertState(sequences[seq][site], seq_type);
            char state = char_to_state[(int)(sequences[seq][site])];
            if (seq_type == SeqType::SEQ_CODON || nt2aa) {
                state = loadCodonState(sequences, site, seq, state, info);
            }
            if (state == STATE_INVALID) {
                recordInvalidCharacter(sequences, site, seq, info);
            }
            pat[seq] = state;
        }
        aln->computeConst(pat);
        if (info.num_error == 0)
        {
            info.isAllGaps = pat.isAllGaps(aln->STATE_UNKNOWN);
        }
        if (progress!=nullptr) {
            (*progress) += (double)step;
        }
    }
}

char PatternInfoVector::loadCodonState(const StrVector& sequences, 
                                       int site, int seq, char state,
                                       PatternInfo& info) {
                    // special treatment for codon
    char state2 = char_to_state[(int)(sequences[seq][site+1])];
    char state3 = char_to_state[(int)(sequences[seq][site+2])];
    if (state < 4 && state2 < 4 && state3 < 4) {
        //state = non_stop_codon[state*16 + state2*4 + state3];
        state = state*16 + state2*4 + state3;
        if (aln->genetic_code[(int)state] == '*') {
            info.errors << "Sequence " << aln->seq_names[seq]
                << " has stop codon " << sequences[seq][site]
                << sequences[seq][site + 1] << sequences[seq][site + 2]
                << " at site " << site + 1 << "\n";
            info.num_error++;
            state = aln->STATE_UNKNOWN;
        } else if (nt2aa) {
            state = AA_to_state[(int)aln->genetic_code[(int)state]];
        } else {
            state = aln->non_stop_codon[(int)state];
        }
    } else if (state == STATE_INVALID || state2 == STATE_INVALID ||
                state3 == STATE_INVALID) {
        state = STATE_INVALID;
    } else {
        if (state != aln->STATE_UNKNOWN || state2 != aln->STATE_UNKNOWN ||
            state3 != aln->STATE_UNKNOWN) {
            info.warnings << "WARNING: Sequence " << aln->seq_names[seq]
                << " has ambiguous character " << sequences[seq][site]
                << sequences[seq][site + 1] << sequences[seq][site + 2]
                << " at site " << site + 1 << "\n";
        }
        state = aln->STATE_UNKNOWN;
    }
    return state;
}

void PatternInfoVector::recordInvalidCharacter(const StrVector& sequences, 
                                               int site, int seq, 
                                               PatternInfo& info) {
    if (info.num_error <= 100) {
        if (info.num_error < 100) {
            info.errors << "Sequence " << aln->seq_names[seq]
                        << " has invalid character " << sequences[seq][site];
            if (seq_type == SeqType::SEQ_CODON) {
                info.errors << sequences[seq][site + 1] << sequences[seq][site + 2];
            }
            info.errors << " at site " << site+1 << endl;
        } else if (info.num_error == 100) {
            info.errors << "...many more..." << endl;
        }
    }
    ++info.num_error;
}

int PatternInfoVector::compressPatterns(int step, std::stringstream& err_str,
                                        progress_display_ptr progress) {
    int w = 0;
    int site = 0;
    for (int r = 0; r < size(); ++r, site+=step) {
        PatternInfo& info     = at(r);
        std::string  warnings = info.warnings.str();
        if (!warnings.empty()) {
            progressHide(progress);
            cout << warnings;
            progressShow(progress);
        }
        std::string errors = info.errors.str();
        if (!errors.empty()) {
            err_str << errors;
        }
        else {
            num_gaps_only += info.isAllGaps ? 1 : 0;
            auto pat_it = aln->pattern_index.find(aln->at(r));
            if (pat_it == aln->pattern_index.end()) {
                if (w < r) {
                    std::swap(aln->at(w), aln->at(r));
                }
                aln->at(w).frequency = 1;
                aln->pattern_index[aln->at(w)] = w;
                aln->site_pattern[r] = w;
                ++w;
            }
            else {
                int q = pat_it->second;
                ++(aln->at(q).frequency);
                aln->site_pattern[r] = q;
            }
        }
        if (progress!=nullptr) {
            (*progress) += ((double)step);
        }
    }
    return w;
}

void processSeq(string &sequence, string &line, int line_num) {
    const char* lineStart = line.data();
    const char* lineEnd = line.data() + line.size();

    for (const char* it = lineStart; it != lineEnd; ++it) {
        if ((*it) <= ' ') continue;
        if (isalnum(*it) || (*it) == '-' || (*it) == '?' ||
            (*it) == '.' || (*it) == '*' || (*it) == '~') {
            sequence.append(1, toupper(*it));
        }
        else if (*it == '(' || *it == '{') {
            auto start_it = it;
            while (*it != ')' && *it != '}' && it != lineEnd)
                ++it;
            if (it == lineEnd) {
                throw "Line " + convertIntToString(line_num) +
                      ": No matching close-bracket ) or } found";
            }
            sequence.append(1, '?');
            cout << "NOTE: Line " << line_num << ": "
                 << line.substr(start_it-lineStart, (it-start_it)+1)
                 << " is treated as unknown character" << endl;
        } else {
            throw "Line " + convertIntToString(line_num) +
                  ": Unrecognized character "  + *it;
        }
    }
}

int Alignment::readPhylip(const char *filename, const char *sequence_type) {

    StrVector sequences;
    ostringstream err_str;
    igzstream in;
    int line_num = 1;
    // set the failbit and badbit
    in.exceptions(ios::failbit | ios::badbit);
    in.open(filename);
    int nseq = 0, nsite = 0;
    int seq_id = 0;
    string line;
    // remove the failbit
    in.exceptions(ios::badbit);
    bool tina_state = (sequence_type && (strcmp(sequence_type,"TINA") == 0 || strcmp(sequence_type,"MULTI") == 0));
    num_states = 0;

    for (; !in.eof(); line_num++) {
        safeGetLine(in, line);
        line = line.substr(0, line.find_first_of("\n\r"));
        if (line == "") continue;

        //cout << line << endl;
        if (nseq == 0) { 
            // read number of sequences and sites
            readFirstLineOfPhylipFile(line, nseq, nsite);
            seq_names.resize(nseq, "");
            sequences.resize(nseq, "");
        } 
        else { // read sequence contents
            if (seq_names[seq_id] == "") { // cut out the sequence name
                string::size_type pos = line.find_first_of(" \t");
                if (pos == string::npos) {
                    pos = 10; //  assume standard phylip
                }
                seq_names[seq_id] = line.substr(0, pos);
                line.erase(0, pos);
            }
            int old_len = static_cast<int>(sequences[seq_id].length());
            if (tina_state) {
                stringstream linestr(line);
                while (!linestr.eof() ) {
                    int state = -1;
                    linestr >> state;
                    if (state < 0) {
                        break;
                    }
                    sequences[seq_id].append(1, state);
                    if (num_states < state+1) {
                        num_states = state+1;
                    }
                }
            } else processSeq(sequences[seq_id], line, line_num);
            if (sequences[seq_id].length() != sequences[0].length()) {
                err_str << "Line " << line_num
                        << ": Sequence " << seq_names[seq_id]
                        << " has wrong sequence length "
                        << sequences[seq_id].length() << endl;
                throw err_str.str();
            }
            if (sequences[seq_id].length() > old_len) {
                seq_id++;
            }
            if (seq_id == nseq) {
                seq_id = 0;
                // make sure that all sequences have the same length at this moment
            }
        }
        //sequences.
    }
    in.clear();
    // set the failbit again
    in.exceptions(ios::failbit | ios::badbit);
    in.close();

    return buildPattern(sequences, sequence_type, nseq, nsite);
}

void Alignment::readFirstLineOfPhylipFile(const std::string& line,
                                          int& nseq, int& nsite) {
    istringstream line_in(line);
    if (!(line_in >> nseq >> nsite)) {
        throw "Invalid PHYLIP format."
                " First line must contain number of sequences and sites";
    }
    //cout << "nseq: " << nseq << "  nsite: " << nsite << endl;
    if (nseq < 3) {
        throw "There must be at least 3 sequences";
    }
    if (nsite < 1) {
        throw "No alignment columns";
    }
}

int Alignment::readPhylipSequential(const char *filename,
                                    const char *sequence_type) {

    StrVector sequences;
    igzstream in;
    int line_num = 1;
    // set the failbit and badbit
    in.exceptions(ios::failbit | ios::badbit);
    in.open(filename);
    int nseq = 0, nsite = 0;
    int seq_id = 0;
    string line;
    // remove the failbit
    in.exceptions(ios::badbit);
    num_states = 0;

    for (; !in.eof(); line_num++) {
        safeGetLine(in, line);
        line = line.substr(0, line.find_first_of("\n\r"));
        if (line == "") continue;

        //cout << line << endl;
        if (nseq == 0) { // read number of sequences and sites
            istringstream line_in(line);
            if (!(line_in >> nseq >> nsite))
                throw "Invalid PHYLIP format."
                      " First line must contain number of sequences and sites";
            //cout << "nseq: " << nseq << "  nsite: " << nsite << endl;
            if (nseq < 3)
                throw "There must be at least 3 sequences";
            if (nsite < 1)
                throw "No alignment columns";

            seq_names.resize(nseq, "");
            sequences.resize(nseq, "");

        } else { // read sequence contents
            if (seq_id >= nseq) {
                throw "Line " + convertIntToString(line_num) +
                      ": Too many sequences detected";
            }
            if (seq_names[seq_id] == "") { // cut out the sequence name
                string::size_type pos = line.find_first_of(" \t");
                if (pos == string::npos) {
                    pos = 10; //  assume standard phylip
                }
                seq_names[seq_id] = line.substr(0, pos);
                line.erase(0, pos);
            }
            processSeq(sequences[seq_id], line, line_num);
            if (sequences[seq_id].length() > nsite)
                throw ("Line " + convertIntToString(line_num) + ": Sequence " 
                    + seq_names[seq_id] + " is too long (" 
                    + convertInt64ToString(sequences[seq_id].length()) + ")");
            if (sequences[seq_id].length() == nsite) {
                seq_id++;
            }
        }
        //sequences.
    }
    in.clear();
    // set the failbit again
    in.exceptions(ios::failbit | ios::badbit);
    in.close();

    return buildPattern(sequences, sequence_type, nseq, nsite);
}

int Alignment::readFasta(const char *filename, const char *sequence_type) {
    StrVector sequences;
    igzstream in;

    // PoMo with Fasta files is not supported yet.
    // if (sequence_type) {
    //     string st (sequence_type);
    //     if (st.substr(0,2) != "CF")
    //         throw "PoMo does not support reading fasta files yet,"
    //               " please use a Counts File.";
    // }

    // set the failbit and badbit
    in.exceptions(ios::failbit | ios::badbit);
    in.open(filename);
    // remove the failbit
    in.exceptions(ios::badbit);

    readFastaSequenceData(in, sequences);

    in.clear();
    // set the failbit again
    in.exceptions(ios::failbit | ios::badbit);
    in.close();

    // now try to cut down sequence name if possible
    int i, step = 0;
    StrVector new_seq_names, remain_seq_names;
    new_seq_names.resize(seq_names.size());
    remain_seq_names = seq_names;

    double startShorten = getRealTime();
    for (step = 0; step < 4; step++) {
        bool duplicated = false;
        unordered_set<string> namesSeenThisTime;
        //Set of shorted names seen so far, this iteration
        for (i = 0; i < seq_names.size(); i++) {
            if (remain_seq_names[i].empty()) continue;
            size_t pos = remain_seq_names[i].find_first_of(" \t");
            if (pos == string::npos) {
                new_seq_names[i] += remain_seq_names[i];
                remain_seq_names[i] = "";
            } else {
                new_seq_names[i] += remain_seq_names[i].substr(0, pos);
                remain_seq_names[i] = "_" + remain_seq_names[i].substr(pos+1);
            }
            if (!duplicated) {
                //add the shortened name for sequence i to the
                //set of shortened names seen so far, and set
                //duplicated to true if it was already there.
                duplicated = !namesSeenThisTime.insert(new_seq_names[i]).second;
            }
        }
        if (!duplicated) break;
    }
    if (verbose_mode >= VerboseMode::VB_MED) {
        cout.precision(6);
        cout << "Name shortening took "
             << (getRealTime() - startShorten) << " seconds." << endl;
    }
    if (step > 0) {
        for (i = 0; i < seq_names.size(); i++)
            if (seq_names[i] != new_seq_names[i]) {
                cout << "NOTE: Change sequence name '" << seq_names[i] << "'"
                     << " -> " << new_seq_names[i] << endl;
            }
    }

    seq_names = new_seq_names;

    return buildPattern(sequences, sequence_type, 
                        static_cast<int>(seq_names.size()), 
                        static_cast<int>(sequences.front().length()));
}

void Alignment::readFastaSequenceData(igzstream& in, StrVector& sequences) {
    // ifstream in;
    int line_num = 1;
    string line;
    #if USE_PROGRESS_DISPLAY
    const char* task = isShowingProgressDisabled ? "" : "Reading fasta file";
    progress_display progress(in.getCompressedLength(), task, "", "");
    #else
    double progress = 0.0;
    #endif
    
    for (; !in.eof(); line_num++) {
        safeGetLine(in, line);
        if (line == "") {
            continue;
        }
        //cout << line << endl;
        if (line[0] == '>') { // next sequence
            string::size_type pos = line.find_first_of("\n\r");
            seq_names.push_back(line.substr(1, pos-1));
            trimString(seq_names.back());
            sequences.push_back("");
            continue;
        }
        // read sequence contents
        if (sequences.empty()) {
            throw "First line must begin with '>' to define sequence name";
        }
        processSeq(sequences.back(), line, line_num);
        progress = (double)in.getCompressedPosition();
    }
    #if USE_PROGRESS_DISPLAY
    progress.done();
    #endif
}


int Alignment::readClustal(const char *filename,
                           const char *sequence_type) {

    StrVector sequences;
    igzstream in;
    int line_num = 1;
    string line;
    num_states = 0;

    // set the failbit and badbit
    in.exceptions(ios::failbit | ios::badbit);
    in.open(filename);
    // remove the failbit
    in.exceptions(ios::badbit);
    safeGetLine(in, line);
    if (line.substr(0, 7) != "CLUSTAL") {
        throw "ClustalW file does not start with 'CLUSTAL'";
    }

    int seq_count = 0;
    for (line_num = 2; !in.eof(); line_num++) {
        safeGetLine(in, line);
        trimString(line);
        if (line == "") {
            seq_count = 0;
            continue;
        }
        if (line[0] == '*' || line[0] == ':' || line[0] == '.') {
            continue; // ignore conservation line
        }

        size_t pos = line.find_first_of(" \t");
        if (pos == string::npos) {
            throw "Line " + convertIntToString(line_num) +
                  ": whitespace not found between" 
                  " sequence name and content";
        }
        string seq_name = line.substr(0, pos);
        if (seq_count == seq_names.size()) {
            seq_names.push_back(seq_name);
            sequences.push_back("");
        } else if (seq_count > seq_names.size()){
            throw "Line " + convertIntToString(line_num) +
                  ": New sequence name is not allowed here";
        } else if (seq_name != seq_names[seq_count]) {
            throw "Line " + convertIntToString(line_num) +
                  ": Sequence name " + seq_name +
                  " does not match previously declared " +seq_names[seq_count];
        }

        line = line.substr(pos+1);
        trimString(line);
        pos = line.find_first_of(" \t");
        line = line.substr(0, pos);
        // read sequence contents
        processSeq(sequences[seq_count], line, line_num);
        seq_count++;
    }
    in.clear();
    // set the failbit again
    in.exceptions(ios::failbit | ios::badbit);
    in.close();

    if (sequences.empty()) {
        throw "No sequences found."
              " Please check input (e.g. newline character)";
    }
    return buildPattern(sequences, sequence_type, 
        static_cast<int>(seq_names.size()), 
        static_cast<int>(sequences.front().length()));
}


int Alignment::readMSF(const char *filename, const char *sequence_type) {
    StrVector sequences;
    igzstream in;
    int line_num = 1;
    string line;
    num_states = 0;

    // set the failbit and badbit
    in.exceptions(ios::failbit | ios::badbit);
    in.open(filename);
    // remove the failbit
    in.exceptions(ios::badbit);
    safeGetLine(in, line);
    if (!contains(line,"MULTIPLE_ALIGNMENT")) {
        throw "MSF file must start with header line MULTIPLE_ALIGNMENT";
    }

    int  seq_len     = 0;
    int  seq_count   = 0;
    bool seq_started = false;

    for (line_num = 2; !in.eof(); line_num++) {
        safeGetLine(in, line);
        trimString(line);
        if (line == "") {
            continue;
        }

        if (line.substr(0, 2) == "//") {
            seq_started = true;
            continue;
        }

        if (line.substr(0,5) == "Name:") {
            if (seq_started) {
                throw "Line " + convertIntToString(line_num) +
                      ": Cannot declare sequence name here";
            }
            parseMSFSequenceNameLine(line, line_num, sequences, seq_len);
            continue;
        }
        if (!seq_started) {
            continue;
        }
        if (seq_names.empty()) {
            throw "No sequence name declared in header";
        }
        if (isdigit(line[0])) {
            continue;
        }
        size_t pos = line.find_first_of(" \t");
        if (pos == string::npos) {
            throw "Line " + convertIntToString(line_num) 
            + ": whitespace not found between" 
            + " sequence name and content - " + line;
        }
        string seq_name = line.substr(0, pos);
        if (seq_name != seq_names[seq_count]) {
            throw "Line " + convertIntToString(line_num) +
                  ": Sequence name " + seq_name +
                  " does not match previously declared " + seq_names[seq_count];
        }
        line = line.substr(pos+1);
        // read sequence contents
        processSeq(sequences[seq_count], line, line_num);
        seq_count++;
        if (seq_count == seq_names.size()) {
            seq_count = 0;
        }
    }
    in.clear();
    // set the failbit again
    in.exceptions(ios::failbit | ios::badbit);
    in.close();
    return buildPattern(sequences, sequence_type, 
        static_cast<int>(seq_names.size()), 
        static_cast<int>(sequences.front().length()));
}

void Alignment::parseMSFSequenceNameLine(std::string line, int line_num,
                                         StrVector&  sequences, int& seq_len) {
    line = line.substr(5);
    trimString(line);
    size_t pos = line.find_first_of(" \t");
    if (pos == string::npos) {
        throw "Line " + convertIntToString(line_num) +
              ": No whitespace found after sequence name";
    }
    string seq_name = line.substr(0,pos);
    seq_names.push_back(seq_name);
    sequences.push_back("");
    pos = line.find("Len:");
    if (pos == string::npos) {
        throw "Line " + convertIntToString(line_num) +
              ": Sequence description does not contain 'Len:'";
    }
    line = line.substr(pos+4);
    trimString(line);
    pos  = line.find_first_of(" \t");
    if (pos == string::npos) {
        throw "Line " + convertIntToString(line_num) +
              ": No whitespace found after sequence length";
    }
    int len;
    line = line.substr(0, pos);
    try {
        len = convert_int(line.c_str());
    } catch (string &str) {
        throw "Line " + convertIntToString(line_num) + ": " + str;
    }
    if (len <= 0) {
        throw "Line " + convertIntToString(line_num) +
              ": Non-positive sequence length not allowed";
    }
    if (seq_len == 0) {
        seq_len = len;
    }
    else if (seq_len != len) {
        throw "Line " + convertIntToString(line_num) +
              ": Sequence length " + convertIntToString(len) +
              " is different from previously defined " +
              convertIntToString(seq_len);
    }
}

namespace {
    class CountFile {
        igzstream         in;
        std::string       line;
        int               line_num;
        std::string       field;
        std::stringstream err_str;

public:
        explicit CountFile(const char* filename) : line_num(0) {
            // Open counts file.
            // Set the failbit and badbit.
            in.exceptions(ios::failbit | ios::badbit);
            in.open(filename);
            // Remove the failbit.
            in.exceptions(ios::badbit);
        }
        void skipCommentLines() {
            // Skip comments.
            do {
                getline(in, line);
                line_num++;
            }
            while (line[0] == '#');
        }
        void parseIdentificationLine(int& npop, int& nsites) {
            // Strings to check counts-file identification line.
            string ftype, npop_str, nsites_str;
            // Read in npop and nsites;
            istringstream ss1(line);
            // Read and check counts file headerline.
            if (!(ss1 >> ftype >> npop_str >> npop >> nsites_str >> nsites)) {
                err_str << "Counts-File identification line could not be read.";
                throw err_str.str();
            }
            if (ftype.compare("COUNTSFILE") != 0 ||
                npop_str.compare("NPOP") !=0 ||
                nsites_str.compare("NSITES") != 0) {
                err_str << "Counts-File identification line could not be read.";
                throw err_str.str();
            }
        }
        void parseHeaderLine(int npop, StrVector& seq_names) {
            // Headerline.
            istringstream ss2(line);

            for (int field_num = 0; (ss2 >> field); field_num++) {
                if (field_num == 0) {
                    if ((field.compare("Chrom") != 0) && (field.compare("CHROM") != 0)) {
                        err_str << "Unrecognized header field " << field << ".";
                        throw err_str.str();
                    }
                }
                else if (field_num == 1) {
                    if ((field.compare("Pos") != 0) && (field.compare("POS") != 0)) {
                        err_str << "Unrecognized header field " << field << ".";
                        throw err_str.str();
                    }
                }
                else {
                    //Read in sequence names.
                    seq_names.push_back(field);
                }
            }
            if (seq_names.size() != static_cast<size_t>(npop)) {
                err_str << "Number of populations in headerline doesn't match NPOP.";
                throw err_str.str();
            }
        }
        void readValuesFromField(const std::string& field, int nnuc, 
                                 IntVector& values) {
            // Delimiters
            //    char const field_delim = '\t';
            char const value_delim = ',';
            // Variables to stream the data.
            std::string val_str;  // String of counts value.
            int         value;    // Actual int value.

            values.clear();
            istringstream valuestream(field);
            // Loop over bases within one population.
            for (; getline(valuestream, val_str, value_delim);) {
                try {
                    value = convert_int(val_str.c_str());
                } catch(string &) {
                    err_str << "Could not read value "
                            << val_str << " on line " << line_num << ".";
                    throw err_str.str();
                }
                values.push_back(value);
            }
            if (values.size() != nnuc) {
                err_str << "Number of bases does not match"
                        << " on line " << line_num << ".";
                throw err_str.str();
            }
        }

        void countNonZeroElements(const IntVector& values, int& id1, int& id2, 
                                  int &sum, int &count) {
            // Read in the data.
            sum = 0;
            count = 0;
            id1 = -1;
            id2 = -1;
            // Sum over elements and count non-zero elements.
            for(auto it = values.begin(); it != values.end(); ++it) {
                // `i` is an iterator object that points to some
                // element of `value`.
                if (*it != 0) {
                    // `i - values.begin()` ranges from 0 to 3 and
                    // determines the nucleotide or allele type.
                    if (id1 == -1) {
                        id1 = static_cast<int>(it - values.begin());
                    }
                    else {
                        id2 = static_cast<int>(it - values.begin());
                    }
                    count++;
                    sum += *it;
                }
            }
        }

        int handleOneNonZeroElement(const SamplingType pomo_sampling_method,
                                    const int num_states, 
                                    const IntVector& values, int id1,
                                    vector<uint32_t>& pomo_sampled_states, 
                                    IntIntMap& pomo_sampled_states_index,
                                    bool &everything_ok) {
            int state;
            if (pomo_sampling_method == SamplingType::SAMPLING_SAMPLED) {
                // Fixed state, state ID is just id1.
                state = id1;
            } else {
                if (values[id1] >= 16384) {
                    cout << "WARNING: Pattern on line "
                         << line_num << " exceeds count limit of 16384." 
                         << std::endl;
                    everything_ok = false;
                }
                uint32_t pomo_state = (id1 | (values[id1]) << 2);
                IntIntMap::iterator pit = pomo_sampled_states_index.find(pomo_state);
                if (pit == pomo_sampled_states_index.end()) { // not found
                    state = static_cast<int>(pomo_sampled_states.size());
                    pomo_sampled_states_index[pomo_state] = state;
                    pomo_sampled_states.push_back(pomo_state);
                } else {
                    state = pit->second;
                }
                state += num_states; // make the state larger than num_states
            }      
            return state;      
        }

        int doBinomialSampling(int nnuc, int N, const IntVector& values, 
                               int sum,  int id1, int id2,
                               IntVector& sampled_values) {
            // Binomial sampling.  2 bases are present.
            for(int k = 0; k < N; k++) {
                int r_int = random_int(sum);
                if (r_int < values[id1]) {
                    sampled_values[id1]++;
                }
                else { 
                    sampled_values[id2]++;
                }
            }
            if (sampled_values[id1] == 0) {
                return id2;
            }
            else if (sampled_values[id2] == 0) {
                return id1;
            }
            else {
                // Index of polymorphism type; ranges from 0 to 5: [AC], [AG],
                // [AT], [CG], [CT], [GT].
                int j;
                if (id1 == 0) {
                    j = id2 - 1;
                }
                else {
                    j = id1 + id2;
                }
                return nnuc + j*(N-2) + j + sampled_values[id1] - 1;
            }
        }

        int handleTwoNonZeroElements(int num_states, const IntVector& values, 
                                     int id1, int id2, 
                                     vector<uint32_t>& pomo_sampled_states, 
                                     IntIntMap& pomo_sampled_states_index,
                                     bool &everything_ok) {
            int state;
            /* BQM 2015-07: store both states now */
            if (values[id1] >= 16384 || values[id2] >= 16384) {
                // Cannot add sites where more than 16384
                // individuals have the same base within one
                // population.
                everything_ok = false;
            }
            uint32_t pomo_state = (id1 | (values[id1]) << 2)
                                | ((id2 | (values[id2]<<2))<<16);
            IntIntMap::iterator pit = pomo_sampled_states_index.find(pomo_state);
            if (pit == pomo_sampled_states_index.end()) { // not found
                uint32_t s = static_cast<uint32_t>(pomo_sampled_states.size());
                state = pomo_sampled_states_index[pomo_state] = s;
                pomo_sampled_states.push_back(pomo_state);
            } else {
                state = pit->second;
            }
            state += num_states; // make the state larger than num_states
            return state;
        }

        bool buildPatternFromCurrentLine(int nnuc, const SamplingType pomo_sampling_method,
                                         const int N, int num_states, int STATE_UNKNOWN,
                                         bool& includes_state_unknown,
                                         int&  n_samples_sum,  int &n_sites_sum, 
                                         vector<uint32_t>& pomo_sampled_states, 
                                         IntIntMap& pomo_sampled_states_index,
                                         Pattern& pattern) {
            // Vector of nucleotide base counts in order A, C, G, T.
            IntVector values;
            // Sampled vector of nucleotide base counts (N individuals are
            // sampled out of =values=).
            IntVector sampled_values;

            // The state a population is in at a specific site.
            // 0 ... 3 = fixed A,C,G,T
            // 4 + j*(N-2)+j ... 4 + (j+1)*(N-2)+j = polymorphism of type j
            // E.g., 4 = [1A,9C]; 5 = [2A,8C]; 12 = [9A,1C]; 13 = [1A,9G]
            int state;

            int su_number = 0;

            pattern.clear();
            bool everything_ok = true;
            istringstream fieldstream(line);
            // Loop over populations / individuals.
            int field_num = 0;
            for ( ; (fieldstream >> field); ) {
                // Skip Chrom and Pos columns.
                if ( (field_num == 0) || (field_num == 1)) {
                    field_num++;
                    continue;
                }
                // Clear value vectors.
                sampled_values.clear();
                sampled_values.resize(nnuc,0);

                readValuesFromField(field, nnuc, values);

                // Variables to convert sampled_values to a state in the pattern.
                int id1;
                int id2;
                int sum;
                int count;
                
                countNonZeroElements(values, id1, id2, sum, count);

                // Determine state (cf. above).
                if (count == 1) {
                    n_samples_sum += values[id1];
                    n_sites_sum++;
                    state = handleOneNonZeroElement(pomo_sampling_method,
                                                    num_states, values, id1,
                                                    pomo_sampled_states, 
                                                    pomo_sampled_states_index,
                                                    everything_ok);
                }
                else if (count == 0) {
                    state = STATE_UNKNOWN;
                    su_number++;
                    includes_state_unknown = true;
                }
                else if (count > 2) {
                    if (verbose_mode >= VerboseMode::VB_MAX) {
                        std::cout << "WARNING: More than two bases are present on line ";
                        std::cout << line_num << "." << std::endl;
                    }
                    everything_ok = false;
                    // err_str << "More than 2 bases are present on line " << line_num << ".";
                    // throw err_str.str();
                }
                // Now we deal with the important polymorphic states with two alleles.
                else if (count == 2) {
                    n_samples_sum += values[id1];
                    n_samples_sum += values[id2];
                    n_sites_sum++;
                    if (pomo_sampling_method == SamplingType::SAMPLING_SAMPLED) {
                        state = doBinomialSampling(nnuc, N, values, sum, 
                                                   id1, id2, sampled_values);
                    } else {
                        state = handleTwoNonZeroElements(num_states, values, 
                                                         id1, id2, 
                                                         pomo_sampled_states, 
                                                         pomo_sampled_states_index,
                                                         everything_ok);
                    }
                }
                else {
                    err_str << "Unexpected error on line number " << line_num << ".";
                    throw err_str.str();
                }
                // Now we have the state to build a pattern ;-).
                pattern.push_back(state);
            }
            return everything_ok;
        }

        void parseData(int npop, int nnuc, const SamplingType pomo_sampling_method,
                       const int N, int num_states, int STATE_UNKNOWN,
                       int& n_samples_sum, int &n_sites_sum,
                       int& site_count, int& fails,
                       vector<uint32_t>& pomo_sampled_states, 
                       IntIntMap& pomo_sampled_states_index,
                       vector<Pattern>& su_buffer, IntVector& su_site_counts,
                       const std::function <void (Pattern& pat, int site)>& addPattern ) {


            // String with states.  Each character represents an integer state
            // value ranging from 0 to 4+(4 choose 2)*(N-1)-1.  E.g., 0 to 57
            // if N is 10.
            Pattern pattern;

            for ( ; getline(in, line); ) {
                line_num++;
                // BQM: not neccessary, su_site_count will be equal to su_site_counts.size()
                //    int su_site_count = 0;
                bool includes_state_unknown = false;
                bool everything_ok = buildPatternFromCurrentLine(nnuc, pomo_sampling_method, 
                                                                 N,  num_states, STATE_UNKNOWN,
                                                                 includes_state_unknown,
                                                                 n_samples_sum, n_sites_sum, 
                                                                 pomo_sampled_states, 
                                                                 pomo_sampled_states_index, 
                                                                 pattern);
                if ((int) pattern.size() != npop) {
                    err_str << "Number of species does not match on line " << line_num << ".";
                    throw err_str.str();
                }
                // Pattern has been built and is now added to the vector of
                // patterns.
                if (everything_ok == true) {
                    if (includes_state_unknown) {
        //                su_site_count++;
                        if (pomo_sampling_method == SamplingType::SAMPLING_WEIGHTED_BINOM ||
                            pomo_sampling_method == SamplingType::SAMPLING_WEIGHTED_HYPER) {
                            su_buffer.push_back(pattern);
                            su_site_counts.push_back(site_count);
                        }
                        // Add pattern if we use random sampling because then,
                        // STATE_UNKNOWN = num_states is well defined already at
                        // this stage.
                        else {
                            addPattern(pattern, site_count);
                        }
                        // BQM: it is neccessary to always increase site_count
                        site_count++;
                    }
                    else {
                        addPattern(pattern, site_count);
                        site_count++;
                    }
                }
                else {
                    fails++;
                    if (verbose_mode >= VerboseMode::VB_MAX) {
                        std::cout << "WARNING: Pattern on line "
                                  << line_num << " was not added." 
                                  << std::endl;
                    }
                }
            }
        }
        ~CountFile() {
            in.clear();
            // set the failbit again
            in.exceptions(ios::failbit | ios::badbit);
            in.close();
        }
    };
};

void Alignment::checkForCustomVirtualPopulationSize
        (const std::string& model_name, int& N) {
    // Check for custom virtual population size or sampling method.
    size_t n_pos_start = model_name.find("+N");
    size_t n_pos_end   = model_name.find_first_of("+", n_pos_start+1);
    if (n_pos_start != string::npos) {
        intptr_t length;
        if (n_pos_end != string::npos) {
            length = n_pos_end - n_pos_start - 2;
        }
        else {
            length = model_name.length() - n_pos_start - 2;
        }
        try {
            N = convert_int(model_name.substr(n_pos_start+2,length).c_str());
        }
        catch (string& str) {
            cout << "The model string is faulty." << endl;
            cout << "The virtual population size N";
            cout << " is not clear when reading in data." << endl;
            cout << "Use, e.g., \"+N7\"." << endl;
            cout << "For each run, N can only be set once." << endl;
            outError(str);
        }
        if (((N != 10) && (N != 2) && (N % 2 == 0)) || (N < 2) || (N > 19)) {
            outError("Custom virtual population size of PoMo"
                     " not 2, 10 or any other odd number between 3 and 19.");
        }
    }
}

void Alignment::checkForCustomSamplingMethod
        (const std::string& model_name, int& N) {
    // TODO: probably remove virtual_pop_size and use N only.
    virtual_pop_size     = N;

    size_t w_pos = model_name.find("+WB");
    size_t h_pos = model_name.find("+WH");
    size_t s_pos = model_name.find("+S");
    int count_sampling_methods = 0;
    if (w_pos != string::npos) {
      pomo_sampling_method = SamplingType::SAMPLING_WEIGHTED_BINOM;
      count_sampling_methods += 1;
    }
    if (h_pos != string::npos) {
      pomo_sampling_method = SamplingType::SAMPLING_WEIGHTED_HYPER;
      count_sampling_methods += 1;
    }
    if (s_pos != string::npos) {
      pomo_sampling_method = SamplingType::SAMPLING_SAMPLED;
      count_sampling_methods += 1;
    }
    if (count_sampling_methods > 1) {
      outError("Multiple sampling methods specified.");
    }
}

// TODO: Use outWarning to print warnings.
int Alignment::readCountsFormat(const char* filename, const char* sequence_type) {
    int N = 9;                   // Virtual population size; defaults
                                 // to 9.  If `-st CFXX` is given, it
                                 // will be set to XX below.
    int nnuc = 4;                // Number of nucleotides (base states).
    ostringstream err_str;

    // Access model_name in global parameters; needed to get N and
    // sampling method.
    Params& params = Params::getInstance();
    // TODO DS: Do not temper with params; use another way to set PoMo flag.
    params.pomo  = true;

    // Initialize sampling method.
    pomo_sampling_method = SamplingType::SAMPLING_WEIGHTED_BINOM;

    checkForCustomVirtualPopulationSize(model_name, N);
    params.pomo_pop_size = N;
    checkForCustomSamplingMethod(model_name, N);

    // Print error if sequence type is given (not supported anymore).
    if (sequence_type) {
        cout << "Counts files are auto detected." << endl;
        cout << "PoMo does not support -st flag." << endl;
        cout << "Please use model string to specify"
             << " virtual population size and sampling method." << endl;
        outError("Abort.");
    }

    // if (sequence_type) {
    //     string st (sequence_type);
    //     if (st.substr(0,2) == "CR")
    //         pomo_random_sampling = true;
    //     else if (st.substr(0,2) == "CF")
    //         pomo_random_sampling = false;
    //     else
    //         throw "Counts File detected but sequence type"
    //               " (-st) is neither 'CF' nor 'CR'.";
    //     string virt_pop_size_str = st.substr(2);
    //     if (virt_pop_size_str != "") {
    //         int virt_pop_size = atoi(virt_pop_size_str.c_str());
    //         N = virt_pop_size;
    //     }
    // }

    // Set the number of states.  If nnuc=4:
    // 4 + (4 choose 2)*(N-1) = 58.
    num_states = nnuc + nnuc*(nnuc-1)/2*(N-1);
    seq_type   = SeqType::SEQ_POMO;

    // Set UNKNOWN_STATE.  This state is set if no information is in
    // the alignment.  If we use partial likelihood we do not know the
    // number of different patterns in the alignment yet and hence,
    // cannot set the variable STATE_UNKNOWN yet (see
    // `state_unknown_buffer`).
    computeUnknownState();

    // Use a buffer for STATE_UNKNOWN.  I.e., if an unknown state is
    // encountered, the pattern is added to this buffer.  Only after
    // all sites have been read in, the patterns from this temporal
    // buffer are added to the normal alignment because then, the
    // value of STATE_UNKNOWN is known.
    vector<Pattern> su_buffer;
    // The site numbers of the patterns that include unknown states.
    IntVector su_site_counts;

    // Variables to calculate mean number of samples per population.
    // If N is way above the average number of samples, PoMo has been
    // ovserved to be unstable and a big warning is printed.
    int n_samples_sum = 0;
    int n_sites_sum = 0;
    // Average number of samples.
    double n_samples_bar = 0;

    CountFile countfile(filename);
    countfile.skipCommentLines();

    int npop   = 0;              // Number of populations.
    int nsites = 0;              // Number of sites.
    countfile.parseIdentificationLine(npop, nsites);

    cout << endl;
    cout << "----------------------------------------------------------------------" << endl;
    cout << "Number of populations:     " << npop << endl;
    cout << "Number of sites:           " << nsites << endl;

    if (nsites > 0)
        site_pattern.resize(nsites);
    else {
        err_str << "Number of sites is 0.";
        throw err_str.str();
    }

    countfile.skipCommentLines();
    countfile.parseHeaderLine(npop, seq_names);

    int site_count = 0;         // Site / base counter.
    int fails      = 0;

    countfile.parseData(npop, nnuc, pomo_sampling_method, N,
                        num_states, STATE_UNKNOWN,
                        n_samples_sum, n_sites_sum, site_count, fails,
                        pomo_sampled_states, pomo_sampled_states_index,
                        su_buffer, su_site_counts,
                        [this](Pattern& pat, int site) { addPattern(pat, site); });

    if (site_count + fails != nsites) {
        err_str << "Number of sites does not match NSITES.";
        throw err_str.str();
    }

    if (pomo_sampling_method == SamplingType::SAMPLING_WEIGHTED_BINOM ||
        pomo_sampling_method == SamplingType::SAMPLING_WEIGHTED_HYPER) {
        // Now we can correctly set STATE_UNKNOWN.
        STATE_UNKNOWN = static_cast<int>(pomo_sampled_states.size()) + num_states;

        // Process sites that include an unknown state.
        for (auto pat_it = su_buffer.begin();
             pat_it != su_buffer.end(); pat_it++) {
            for (auto sp_it = pat_it->begin(); sp_it != pat_it->end(); sp_it++)
                if (*sp_it == 0xffffffff) *sp_it = STATE_UNKNOWN;
        }

        for (unsigned int i = 0; i < su_buffer.size(); i++) {
            addPattern(su_buffer[i], su_site_counts[i]);
        }
    }

    cout << "---" << endl;
    cout << "Normal sites:              " << site_count - su_site_counts.size() << endl;
    cout << "Sites with unknown states: " << su_site_counts.size() << endl;
    cout << "Total sites read:          " << site_count << endl;
    cout << "Fails:                     " << fails << endl;
    if (pomo_sampling_method == SamplingType::SAMPLING_WEIGHTED_BINOM ||
        pomo_sampling_method == SamplingType::SAMPLING_WEIGHTED_HYPER) {
        cout << "---" << endl;
        cout << "Compound states:           " << pomo_sampled_states.size() << endl;
    }
    cout << "----------------------------------------------------------------------" << endl << endl;

    // Check if N is not too large.
    n_samples_bar = n_samples_sum / (double) n_sites_sum;
    cout << "The average number of samples is " << n_samples_bar << endl;
    if ((pomo_sampling_method == SamplingType::SAMPLING_WEIGHTED_BINOM) &&
        (n_samples_bar * 3.0 <= N)) {
        cout << "----------------------------------------------------------------------" << endl;
        cout << "WARNING: The virtual population size N is much larger ";
        cout << "than the average number of samples." << endl;
        cout << "WARNING: This setting together with /weighted binomial/ sampling ";
        cout << "may be numerically unstable." << endl << endl;
        cout << "----------------------------------------------------------------------" << endl;
    }
    site_pattern.resize(site_count);

    return 1;
}

bool Alignment::getSiteFromResidue(int seq_id, int &residue_left,
                                   int &residue_right) const {
    int i, j;
    int site_left = -1, site_right = -1;
    for (i = 0, j = -1; i < getNSite(); i++) {
        if (at(site_pattern[i])[seq_id] != STATE_UNKNOWN) j++;
        if (j == residue_left) {
            site_left = i;
        }
        if (j == residue_right - 1) {
            site_right = i + 1;
        }
    }
    if (site_left < 0 || site_right < 0) {
        cout << "Out of range: Maxmimal residue number is " 
             << j+1 << endl;
    }
    if (site_left == -1) {
        outError("Left residue range is too high");
    }
    if (site_right == -1) {
        outWarning("Right residue range is set to alignment length");
        site_right = getNSite32();
    }
    residue_left  = site_left;
    residue_right = site_right;
    return true;
}

int Alignment::buildRetainingSites(const char *aln_site_list, IntVector &kept_sites,
                                   int exclude_sites, const char *ref_seq_name) const {
    if (aln_site_list) {
        intptr_t seq_id = -1;
        if (ref_seq_name) {
            string ref_seq = ref_seq_name;
            seq_id = getSeqID(ref_seq);
            if (seq_id < 0) {
                outError("Reference sequence name not found: ", ref_seq_name);
            }
        }
        cout << "Reading site position list " << aln_site_list << " ..." << endl;
        kept_sites.resize(getNSite(), 0);
        try {
            ifstream in;
            in.exceptions(ios::failbit | ios::badbit);
            in.open(aln_site_list);
            in.exceptions(ios::badbit);

            while (!in.eof()) {
                int left, right;
                left = right = 0;
                in >> left;
                if (in.eof()) break;
                in >> right;
                cout << left << "-" << right << endl;
                if (left <= 0 || right <= 0) {
                    throw "Range must be positive";
                }
                if (left > right) {
                    throw "Left range is bigger than right range";
                }
                left--;
                if (right > getNSite()) {
                    throw "Right range is bigger than alignment size";
                }
                if (seq_id >= 0) {
                    getSiteFromResidue(static_cast<int>(seq_id), left, right);
                }
                for (int i = left; i < right; i++)
                    kept_sites[i] = 1;
            }
            in.close();
        } catch (ios::failure) {
            outError(ERR_READ_INPUT, aln_site_list);
        } catch (const char* str) {
            outError(str);
        }
    } else {
        kept_sites.resize(getNSite(), 1);
    }

    int j;
    if (exclude_sites & EXCLUDE_GAP) {
        for (j = 0; j < kept_sites.size(); j++)
            if (kept_sites[j] && at(site_pattern[j]).computeAmbiguousChar(num_states) > 0) {
                kept_sites[j] = 0;
            }
    }
    if (exclude_sites & EXCLUDE_INVAR) {
        for (j = 0; j < kept_sites.size(); j++) {
        	if (at(site_pattern[j]).isInvariant()) {
        		kept_sites[j] = 0;
            }
        }
    }
    if (exclude_sites & EXCLUDE_UNINF) {
        for (j = 0; j < kept_sites.size(); j++) {
            if (!at(site_pattern[j]).isInformative()) {
                kept_sites[j] = 0;
            }
        }
    }
    int final_length = 0;
    for (j = 0; j < kept_sites.size(); j++) {
        if (kept_sites[j]) {
            final_length++;
        }
    }
    return final_length;
}

void Alignment::getStateStrings(StrVector& stateStrings) const {
    //Precalculate state representation strings
    stateStrings.resize(this->num_states);
    for (int i=0; i<num_states; ++i) {
        stateStrings[i] = convertStateBackStr(i);
    }
}

void Alignment::getOneSequence(const StrVector& stateStrings,
                               size_t seq_id, string& str) const {
    auto patterns     = site_pattern.data();
    auto patternCount = site_pattern.size();
    for (int i=0; i<patternCount; ++i) {
        int state = static_cast<int>(at(patterns[i])[seq_id]);
        if (num_states<=state) {
            str.append(convertStateBackStr(state));
        } else {
            str.append(stateStrings[state]);
        }
    }
    str.append("\n");
}

void Alignment::getAllSequences(const char* task_description,
                                StrVector& seq_data) const {
    StrVector stateStrings;
    getStateStrings(stateStrings);
    //Calculate sequence data in parallel
    intptr_t seq_count = seq_names.size();
    seq_data.clear();
    seq_data.resize(seq_count);
    
    #if USE_PROGRESS_DISPLAY
    progress_display contentProgress(seq_count, task_description);
    #else
    double contentProgress = 0;
    #endif
    
    #ifdef _OPENMP
    #pragma omp parallel for
    #endif
    for (intptr_t seq_id = 0; seq_id < seq_count; seq_id++) {
        getOneSequence(stateStrings, seq_id, seq_data[seq_id]);
        if ((seq_id%100)==99) {
            contentProgress += 100.0;
        }
    }
    contentProgress += seq_count % 100;
    #if USE_PROGRESS_DISPLAY
    contentProgress.done();
    #endif
}

void Alignment::printPhylip(ostream &out, bool append,
                            const char *aln_site_list,
                            int exclude_sites, const char *ref_seq_name,
                            bool print_taxid,
                            bool report_progress) const {
    IntVector kept_sites;
    int final_length = buildRetainingSites(aln_site_list, kept_sites,
                                           exclude_sites, ref_seq_name);
    if (seq_type == SeqType::SEQ_CODON) {
        final_length *= 3;
    }
    out << getNSeq() << " " << final_length << endl;
    size_t max_len = getMaxSeqNameLength();
    if (print_taxid) {
        max_len = 10;
    }
    if (max_len < 10) {
        max_len = 10;
    }

    StrVector seq_data;
    const char* calc_description  = "";
    const char* write_description = "";
    if (report_progress) {
        calc_description  = "Calculating content to write to Phylip file";
        write_description = "Writing Phylip file";
    }
    getAllSequences(calc_description, seq_data);
    
    #if USE_PROGRESS_DISPLAY
    auto seq_count = seq_names.size();
    progress_display writeProgress(seq_count, write_description);
    #else
    double writeProgress = 0.0;
    #endif
    
    for (size_t seq_id = 0; seq_id < seq_names.size(); seq_id++) {
        out.width(max_len);
        if (print_taxid) {
            out << left << seq_id << " ";
        }
        else {
            out << left << seq_names[seq_id] << " ";
        }
        std::string& str = seq_data[seq_id];
        out.width(0);
        out.write(str.c_str(), str.length());
        ++writeProgress;
    }
    #if USE_PROGRESS_DISPLAY
    writeProgress.done();
    #endif
}

void Alignment::printFasta(ostream &out, bool append, const char *aln_site_list,
                           int exclude_sites, const char *ref_seq_name,
                           bool report_progress) const
{
    IntVector kept_sites;
    buildRetainingSites(aln_site_list, kept_sites, exclude_sites, ref_seq_name);
    int seq_id = 0;
    for (auto it = seq_names.begin(); it != seq_names.end(); ++it, ++seq_id) {
        out << ">" << (*it) << endl;
        int j = 0;
        for (auto i = site_pattern.begin();  i != site_pattern.end(); ++i, ++j) {
            if (kept_sites[j]) {
                out << convertStateBackStr(at(*i)[seq_id]);
            }
        }
        out << endl;
    }
}

void Alignment::printNexus(ostream &out, bool append, const char *aln_site_list,
                           int exclude_sites, const char *ref_seq_name, 
                           bool print_taxid, bool report_progress) const {
    IntVector kept_sites;
    int final_length = buildRetainingSites(aln_site_list, kept_sites,
                                           exclude_sites, ref_seq_name);
    if (seq_type == SeqType::SEQ_CODON)
        final_length *= 3;
    
    out << "#nexus" << endl << "begin data;" << endl;
    out << "  dimensions ntax=" << getNSeq();
    out << " nchar=" << final_length << ";" << endl;
    out << "  format datatype=";
    switch (seq_type) {
        case SeqType::SEQ_DNA:
        case SeqType::SEQ_CODON:
            out << "nucleotide"; break;
        case SeqType::SEQ_MORPH:
        case SeqType::SEQ_BINARY:
        case SeqType::SEQ_MULTISTATE:
            out << "standard"; break;
        case SeqType::SEQ_PROTEIN:
            out << "protein"; break;
        default:
            outError("Unspported datatype for NEXUS file");
    }
    out << " missing=? gap=-;" << endl;
    out << "  matrix" << endl;
    size_t max_len = getMaxSeqNameLength();
    if (print_taxid) max_len = 10;
    if (max_len < 10) max_len = 10;
    intptr_t nseq = seq_names.size();
    for (intptr_t seq_id = 0; seq_id < nseq; seq_id++) {
        out << "  ";
        out.width(max_len);
        if (print_taxid) {
            out << left << seq_id << " ";
        }
        else {
            out << left << seq_names[seq_id] << " ";
        }
        int j = 0;
        for (auto i = site_pattern.begin();  i != site_pattern.end(); ++i, ++j) {
            if (kept_sites[j]) {
                out << convertStateBackStr(at(*i)[seq_id]);
            }
        }
        out << endl;
    }
    out << "  ;" << endl;
    out << "end;" << endl;
    
}

void Alignment::printAlignment(InputType format, const char *file_name,
                               bool append, const char *aln_site_list,
                               int exclude_sites, const char *ref_seq_name,
                               bool report_progress) {
    try {
        ofstream out;
        out.exceptions(ios::failbit | ios::badbit);
        
        if (append) {
            out.open(file_name, ios_base::out | ios_base::app);
        } else {
            out.open(file_name);
        }
        printAlignment(format, out, file_name, append,
                       aln_site_list, exclude_sites, ref_seq_name,
                       report_progress);

        out.close();
        if (verbose_mode >= VerboseMode::VB_MED || !append) {
            cout << "Alignment was printed to " << file_name << endl;
        }
    } catch (ios::failure) {
        outError(ERR_WRITE_OUTPUT, file_name);
    }
}

void Alignment::printAlignment(InputType format, ostream &out, const char* file_name
                               , bool append,       const char *aln_site_list
                               , int exclude_sites, const char *ref_seq_name
                               , bool report_progress) const {
    double printStart = getRealTime();
    const char* formatName = "phylip";
    switch (format) {
        case InputType::IN_PHYLIP:
            printPhylip(out, append, aln_site_list, exclude_sites, 
                        ref_seq_name, false, report_progress);
            break;
        case InputType::IN_FASTA:
            formatName = "fasta";
            printFasta(out, append, aln_site_list, exclude_sites, 
                       ref_seq_name, report_progress);
            break;
        case InputType::IN_NEXUS:
            formatName = "nexus";
            printNexus(out, append, aln_site_list, exclude_sites, 
                       ref_seq_name, false, report_progress);
            break;
        default:
            ASSERT(0 && "Unsupported alignment output format");
    }
    if (verbose_mode >= VerboseMode::VB_MED && report_progress) {
        std::stringstream msg;
        msg.precision(4);
        msg << "Printing alignment to " << formatName << " file "
            << file_name << " took " << (getRealTime()-printStart)
            << " sec";
        std::cout << msg.str() << std::endl;
    }
}

void Alignment::extractSubAlignment(Alignment *aln, IntVector &seq_id,
                                    int min_true_char, int min_taxa,
                                    IntVector *kept_partitions) {
    IntVector::iterator it;
    aln->seq_to_subset.resize(aln->seq_names.size(),0);
    for (int i : seq_id) {
        ASSERT( 0 <= i && i < aln->getNSeq());
        seq_names.push_back(aln->getSeqName(i));
        seq_to_subset.push_back(aln->getSequenceSubset(i));
    }
    name           = aln->name;
    model_name     = aln->model_name;
    position_spec  = aln->position_spec;
    aln_file       = aln->aln_file;
    copyStateInfoFrom(aln);
    site_pattern.resize(aln->getNSite(), -1);
    clear();
    pattern_index.clear();
    size_t removed_sites = 0;
    VerboseMode save_mode = verbose_mode;
    verbose_mode = min(verbose_mode, VerboseMode::VB_MIN);
    // to avoid printing gappy sites in addPattern
    
    progress_display_ptr progress = nullptr;
    if (!isShowingProgressDisabled) {
        #if USE_PROGRESS_DISPLAY
            progress = new progress_display(aln->getNSite(),
                                            "Identifying sites to remove",
                                            "examined", "site");
        #endif
    }
    size_t oldPatternCount = size(); //JB 27-Jul-2020 Parallelized
    int    siteMod = 0; //site # modulo 100.
    size_t seqCount = seq_id.size();
    for (size_t site = 0; site < aln->getNSite(); ++site) {
        iterator pit = aln->begin() + (aln->getPatternID(site));
        Pattern pat;
        for (it = seq_id.begin(); it != seq_id.end(); ++it) {
            pat.push_back ( (*pit)[*it] );
        }
        size_t true_char = seqCount - pat.computeGapChar(num_states, STATE_UNKNOWN);
        if (true_char < min_true_char) {
            removed_sites++;
        }
        else {
            bool gaps_only = false;
            addPatternLazy(pat, site-removed_sites, 1, gaps_only);
            //JB 27-Jul-2020 Parallelized
        }
        if (progress!=nullptr) {
            if (siteMod == 100 ) {
                (*progress) += 100.0;
                siteMod  = 0;
            }
            ++siteMod;
        }
    }
    progressDone(progress);
    progressDelete(progress);

    updatePatterns(oldPatternCount); //JB 27-Jul-2020 Parallelized
    site_pattern.resize(aln->getNSite() - removed_sites);
    verbose_mode = save_mode;
    countConstSite();
//    buildSeqStates();
    ASSERT(size() <= aln->size());
    if (kept_partitions) {
        kept_partitions->push_back(0);
    }
}


void Alignment::extractPatterns(Alignment *aln, IntVector &ptn_id) {
    intptr_t nseq =  aln->getNSeq();
    seq_to_subset.resize(seq_names.size(),0);
    for (int i = 0; i < nseq; ++i) {
        seq_names.push_back(aln->getSeqName(i));
        seq_to_subset.push_back(aln->getSequenceSubset(i));
    }
    name           = aln->name;
    model_name     = aln->model_name;
    position_spec  = aln->position_spec;
    aln_file       = aln->aln_file;
    copyStateInfoFrom(aln);
    site_pattern.resize(aln->getNSite(), -1);
    clear();
    pattern_index.clear();
    int site = 0;
    VerboseMode save_mode = verbose_mode;
    verbose_mode = min(verbose_mode, VerboseMode::VB_MIN);
    // to avoid printing gappy sites in addPattern
    for (size_t i = 0; i != ptn_id.size(); ++i) {
        ASSERT(ptn_id[i] >= 0 && ptn_id[i] < aln->getNPattern());
        Pattern pat = aln->at(ptn_id[i]);
        addPattern(pat, site, aln->at(ptn_id[i]).frequency);
        for (int j = 0; j < aln->at(ptn_id[i]).frequency; j++)
            site_pattern[site++] = static_cast<int>(size())-1;
    }
    site_pattern.resize(site);
    verbose_mode = save_mode;
    countConstSite();
//    buildSeqStates();
    ASSERT(size() <= aln->size());
}

void Alignment::extractPatternFreqs(Alignment *aln, IntVector &ptn_freq) {
    ASSERT(static_cast<intptr_t>(ptn_freq.size()) <= aln->getNPattern());
    seq_to_subset.resize(seq_names.size(),0);
    for (int i = 0; i < aln->getNSeq(); ++i) {
        seq_names.push_back(aln->getSeqName(i));
        seq_to_subset.push_back(aln->getSequenceSubset(i));
    }
    name          = aln->name;
    model_name    = aln->model_name;
    position_spec = aln->position_spec;
    aln_file      = aln->aln_file;
    copyStateInfoFrom(aln);
    site_pattern.resize(accumulate(ptn_freq.begin(), ptn_freq.end(), 0), -1);
    clear();
    pattern_index.clear();
    int site = 0;
    VerboseMode save_mode = verbose_mode;
    verbose_mode = min(verbose_mode, VerboseMode::VB_MIN);
    // to avoid printing gappy sites in addPattern
    for (size_t i = 0; i != ptn_freq.size(); ++i)
        if (ptn_freq[i]) {
            ASSERT(ptn_freq[i] > 0);
            Pattern pat = aln->at(i);
            addPattern(pat, site, ptn_freq[i]);
            for (int j = 0; j < ptn_freq[i]; j++) {
                site_pattern[site++] = static_cast<int>(size()) - 1;
            }
        }
    site_pattern.resize(site);
    verbose_mode = save_mode;
    countConstSite();
    ASSERT(size() <= aln->size());
}

void Alignment::extractSites(Alignment *aln, IntVector &site_id) {
    seq_to_subset.resize(seq_names.size(),0);
    for (int i = 0; i < aln->getNSeq(); ++i) {
        seq_names.push_back(aln->getSeqName(i));
        seq_to_subset.push_back(aln->getSequenceSubset(i));
    }
    name = aln->name;
    model_name = aln->model_name;
    position_spec = aln->position_spec;
    aln_file = aln->aln_file;
    copyStateInfoFrom(aln);
    site_pattern.resize(site_id.size(), -1);
    clear();
    pattern_index.clear();
    VerboseMode save_mode = verbose_mode;
    verbose_mode = min(verbose_mode, VerboseMode::VB_MIN);
    // to avoid printing gappy sites in addPattern
    for (int i = 0; i != site_id.size(); i++) {
        Pattern pat = aln->getPattern(site_id[i]);
        addPattern(pat, i);
    }
    verbose_mode = save_mode;
    countConstSite();
    // sanity check
    for (iterator it = begin(); it != end(); ++it) {
        if (it->at(0) == -1) {
            ASSERT(0);
        }
    }
    //cout << getNSite() << " positions were extracted" << endl;
    //cout << __func__ << " " << num_states << endl;
}



void Alignment::convertToCodonOrAA(Alignment *aln, char *gene_code_id,
                                   bool nt2aa) {
    if (aln->seq_type != SeqType::SEQ_DNA) {
        outError("Cannot convert non-DNA alignment"
                 " into codon alignment");
    }
    if (aln->getNSite() % 3 != 0) {
        outError("Sequence length is not divisible by 3"
                 " when converting to codon sequences");
    }
    char AA_to_state[NUM_CHAR];
    intptr_t nseqs = aln->getNSeq();
    seq_to_subset.resize(seq_names.size(),0);
    for (intptr_t i = 0; i < nseqs ; ++i) {
        seq_names.push_back(aln->getSeqName(i));
        seq_to_subset.push_back(aln->getSequenceSubset(i));
    }
    name          = aln->name;
    model_name    = aln->model_name;
    sequence_type = aln->sequence_type;
    position_spec = aln->position_spec;
    aln_file      = aln->aln_file;
    //num_states  = aln->num_states;
    seq_type      = SeqType::SEQ_CODON;
    initCodon(gene_code_id, nt2aa);

    computeUnknownState();

    if (nt2aa) {
        buildStateMap(AA_to_state, SeqType::SEQ_PROTEIN);
    }
    site_pattern.resize(aln->getNSite()/3, -1);
    clear();
    pattern_index.clear();
    int step = ((seq_type == SeqType::SEQ_CODON || nt2aa) ? 3 : 1);

    VerboseMode save_mode = verbose_mode;
    verbose_mode  = min(verbose_mode, VerboseMode::VB_MIN);
    // to avoid printing gappy sites in addPattern
    size_t  nsite = aln->getNSite();
    size_t  nseq  = aln->getNSeq();
    Pattern pat;

    pat.resize(nseq);
    int num_error = 0;
    ostringstream err_str;

    for (size_t site = 0; site < nsite; site+=step) {
        convertSiteToCodonOrAA(aln, nt2aa, AA_to_state, 
                               site, pat, num_error, err_str);
        if (!num_error) {
            addPattern(pat, static_cast<int>(site/step));
        }
    }
    if (num_error) {
        outError(err_str.str());
    }
    verbose_mode = save_mode;
    countConstSite();
    // sanity check
    for (iterator it = begin(); it != end(); ++it) {
    	if (it->at(0) == -1) {
    		ASSERT(0);
        }
    }
}

void Alignment::convertSiteToCodonOrAA(Alignment* aln, bool nt2aa,
                                       const char* AA_to_state, 
                                       size_t site, Pattern& pat,
                                       int&   num_error,
                                       std::ostringstream& err_str) {
    size_t nseq = aln->getNSeq();                                        
    for (size_t seq = 0; seq < nseq; ++seq) {
        //char state = convertState(sequences[seq][site], seq_type);
        char state  = aln->at(aln->getPatternID(site))[seq];
        // special treatment for codon
        char state2 = aln->at(aln->getPatternID(site+1))[seq];
        char state3 = aln->at(aln->getPatternID(site+2))[seq];
        if (state < 4 && state2 < 4 && state3 < 4) {
    //            		state = non_stop_codon[state*16 + state2*4 + state3];
            state = state*16 + state2*4 + state3;
            if (genetic_code[(int)state] == '*') {
                err_str << "Sequence " << seq_names[seq]
                        << " has stop codon "
                        << " at site " << site+1 << endl;
                num_error++;
                state = STATE_UNKNOWN;
            } else if (nt2aa) {
                state = AA_to_state[(int)genetic_code[(int)state]];
            } else {
                state = non_stop_codon[(int)state];
            }
        } else if (state == STATE_INVALID || state2 == STATE_INVALID ||
                    state3 == STATE_INVALID) {
            state = STATE_INVALID;
        } else {
            if (state != STATE_UNKNOWN || state2 != STATE_UNKNOWN ||
                state3 != STATE_UNKNOWN) {
                ostringstream warn_str;
                warn_str << "Sequence " << seq_names[seq]
                            << " has ambiguous character "
                            << " at site " << site+1;
                outWarning(warn_str.str());
            }
            state = STATE_UNKNOWN;
        }
        reportIfStateInvalid(site, seq, state, num_error, err_str);
        pat[seq] = state;
    }
}

void Alignment::reportIfStateInvalid(size_t site, size_t seq,
                                     char state,  int&   num_error,
                                     std::ostringstream& err_str) {
    if (state != STATE_INVALID) {
        return;
    }
    if (num_error < 100) {
        err_str << "Sequence " << seq_names[seq]
                << " has invalid character ";
        err_str << " at site " << site+1 << endl;
    } else if (num_error == 100) {
        err_str << "...many more..." << endl;
    }
    num_error++;
}


Alignment *Alignment::convertCodonToAA() {
    Alignment *res = new Alignment;
    if (seq_type != SeqType::SEQ_CODON)
        outError("Cannot convert non-codon alignment into AA");
    char AA_to_state[NUM_CHAR];
    intptr_t nseq = getNSeq();
    res->seq_to_subset.resize(res->seq_names.size(),0);
    for (intptr_t i = 0; i < nseq; ++i) {
        res->seq_names.push_back(getSeqName(i));
        res->seq_to_subset.push_back(getSequenceSubset(i));
    }
    res->name = name;
    res->model_name = model_name;
    res->sequence_type = sequence_type;
    res->position_spec = position_spec;
    res->aln_file = aln_file;
    res->seq_type = SeqType::SEQ_PROTEIN;
    res->num_states = 20;
    
    res->computeUnknownState();
    
    res->buildStateMap(AA_to_state, SeqType::SEQ_PROTEIN);

    res->site_pattern.resize(getNSite(), -1);
    res->clear();
    res->pattern_index.clear();
    
    VerboseMode save_mode = verbose_mode;
    verbose_mode = min(verbose_mode, VerboseMode::VB_MIN);
    // to avoid printing gappy sites in addPattern
    size_t   nsite = getNSite();
    Pattern pat;
    pat.resize(nseq);
    
    for (size_t site = 0; site < nsite; ++site) {
        for (intptr_t seq = 0; seq < nseq; ++seq) {
            StateType state = at(getPatternID(site))[seq];
            if (state == STATE_UNKNOWN) {
                state = res->STATE_UNKNOWN;
            }
            else {
                state = AA_to_state[(int)genetic_code[codon_table[state]]];
            }
            pat[seq] = state;
        }
        res->addPattern(pat, static_cast<int>(site));
    }
    verbose_mode = save_mode;
    res->countConstSite();
    return res;
}

Alignment *Alignment::convertCodonToDNA() {
    Alignment *res = new Alignment;
    if (seq_type != SeqType::SEQ_CODON) {
        outError("Cannot convert non-codon alignment into DNA");
    }
    intptr_t nseqs = getNSeq();
    res->seq_to_subset.resize(res->seq_names.size(),0);
    for (intptr_t i = 0; i < nseqs; ++i) {
        res->seq_names.push_back(getSeqName(i));
        res->seq_to_subset.push_back(getSequenceSubset(i));
    }
    res->name = name;
    res->model_name = model_name;
    res->sequence_type = sequence_type;
    res->position_spec = position_spec;
    res->aln_file = aln_file;
    res->seq_type = SeqType::SEQ_DNA;
    res->num_states = 4;
    
    res->computeUnknownState();
    
    res->site_pattern.resize(getNSite()*3, -1);
    res->clear();
    res->pattern_index.clear();
    
    VerboseMode save_mode = verbose_mode;
    verbose_mode = min(verbose_mode, VerboseMode::VB_MIN);
    // to avoid printing gappy sites in addPattern
    size_t nsite = getNSite();
    size_t nseq  = getNSeq();
    Pattern pat[3];
    pat[0].resize(nseq);
    pat[1].resize(nseq);
    pat[2].resize(nseq);

    for (size_t site = 0; site < nsite; ++site) {
        for (size_t seq = 0; seq < nseq; ++seq) {
            StateType state = at(getPatternID(site))[seq];
            if (state == STATE_UNKNOWN) {
                for (int i = 0; i < 3; ++i)
                    pat[i][seq] = res->STATE_UNKNOWN;
            } else {
                state = codon_table[state];
                pat[0][seq] = state/16;
                pat[1][seq] = (state%16)/4;
                pat[2][seq] = state%4;
            }
        }
        for (int i = 0; i < 3; ++i) {
            res->addPattern(pat[i], static_cast<int>(site * 3 + i));
        }
    }
    verbose_mode = save_mode;
    res->countConstSite();
//    res->buildSeqStates();
    return res;
}

void parse_lower_bound(const char *str, int &lower, char* &endptr) {
    // parse the lower bound of the range
    int d = strtol(str, &endptr, 10);
    if ((d == 0 && endptr == str) || abs(d) == HUGE_VALL) {
        string err = "Expecting integer, but found \"";
        err += str;
        err += "\" instead";
        throw err;
    }
    lower = d;
}

void skip_blank_chars(char* &endptr) {
    // skip blank chars
    for (; *endptr == ' '; endptr++) {}
}

void parse_upper_bound(const char *str, int lower, int &upper, char* &endptr) {
    // parse the upper bound of the range
    skip_blank_chars(endptr);
    str = endptr;
    int d = strtol(str, &endptr, 10);
    if ((d == 0 && endptr == str) || abs(d) == HUGE_VALL) {
        if (str[0] == '.') {
            // 2019-06-03: special character '.' for whatever ending position
            d = lower-1;
            endptr++;
        } else {
            string err = "Expecting integer, but found \"";
            err += str;
            err += "\" instead";
            throw err;
        }
    }
    upper = d;
}

void parse_step_size(const char *str, int &step_size, char* &endptr) {
    if (*endptr != '\\') {
        return;
    }
    // parse the step size of the range
    str = endptr+1;
    int d = strtol(str, &endptr, 10);
    if ((d == 0 && endptr == str) || abs(d) == HUGE_VALL) {
        string err = "Expecting integer, but found \"";
        err += str;
        err += "\" instead";
        throw err;
    }
    step_size = d;
}

void convert_range(const char *str, int &lower, int &upper, 
                   int &step_size, char* &endptr) {
    parse_lower_bound(str, lower, endptr);
    upper     = lower;
    step_size = 1;
    skip_blank_chars(endptr);
    if (*endptr != '-') {
        return;
    } else {
        ++endptr; //skip over the '-' character
    }
    parse_upper_bound(str, lower, upper, endptr);
    skip_blank_chars (endptr);
    parse_step_size  (str, step_size, endptr);
}

void extractSiteID(Alignment *aln, const char* spec,
                   IntVector &site_id) {
    const char* str    = spec;
    try {
        int nchars = 0;
        for (; *str != 0; ) {
            int   lower, upper, step;
            char* endstr;
            convert_range(str, lower, upper, step, endstr);
            str = endstr;
            // 2019-06-03: special '.' character
            if (upper == lower - 1) {
                upper = static_cast<int>(aln->getNSite());
            }
            lower--;
            upper--;
            nchars += (upper-lower+1)/step;
            if (aln->seq_type == SeqType::SEQ_CODON) {
                lower /= 3;
                upper /= 3;
            }
            if (upper >= aln->getNSite()) throw "Too large site ID";
            if (lower < 0) throw "Negative site ID";
            if (lower > upper) throw "Wrong range";
            if (step < 1) throw "Wrong step size";
            for (int i = lower; i <= upper; i+=step) {
                site_id.push_back(i);
            }
            if (*str == ',' || *str == ' ') str++;
            //else break;
        }
        if (aln->seq_type == SeqType::SEQ_CODON && nchars % 3 != 0) {
            throw (string)"Range " + spec +
                  " length is not multiple of 3 (necessary for codon data)";
        }
    } catch (const char* err) {
        outError(err);
    } catch (string err) {
        outError(err);
    }
}

void Alignment::extractSites(Alignment *aln, const char* spec) {
    IntVector site_id;
    extractSiteID(aln, spec, site_id);
    extractSites(aln, site_id);
}

void Alignment::createBootstrapAlignment(Alignment *aln,
                                         IntVector* pattern_freq,
                                         const char *spec)  {
    if (aln->isSuperAlignment()) {
        outError("Internal error: ", __func__);
    }
    name = aln->name;
    model_name = aln->model_name;
    position_spec = aln->position_spec;
    aln_file = aln->aln_file;
    int nsite = aln->getNSite32();
    seq_names.insert(seq_names.begin(),
                     aln->seq_names.begin(), aln->seq_names.end());
    copyStateInfoFrom(aln);
    site_pattern.resize(nsite, -1);
    clear();
    pattern_index.clear();

    // 2016-07-05: copy variables for PoMo
    pomo_sampled_states = aln->pomo_sampled_states;
    pomo_sampled_states_index = aln->pomo_sampled_states_index;
    pomo_sampling_method = aln->pomo_sampling_method;
    virtual_pop_size = aln->virtual_pop_size;

    VerboseMode save_mode = verbose_mode;
    verbose_mode = min(verbose_mode, VerboseMode::VB_MIN);
    // to avoid printing gappy sites in addPattern
    if (pattern_freq) {
        pattern_freq->resize(0);
        pattern_freq->resize(aln->getNPattern(), 0);
    }

    if (!aln->site_state_freq.empty()) {
        // resampling also the per-site state frequency vector
        if (aln->site_state_freq.size() != aln->getNPattern() || spec)
            outError("Unsupported bootstrap feature,"
                     " please contact the developers");
    }
    
    if (Params::getInstance().jackknife_prop > 0.0 && spec) {
        outError((string)"Unsupported jackknife with sampling " + spec);
    }

	IntVector site_vec;
    if (!spec) {
		// standard bootstrap
        int added_sites = 0;
        IntVector sample;
        random_resampling(nsite, sample);
        for (int site = 0; site < nsite; ++site) {
            for (int rep = 0; rep < sample[site]; ++rep) {
                int ptn_id = aln->getPatternID(site);
                Pattern pat = aln->at(ptn_id);
                intptr_t nptn = getNPattern();
                addPattern(pat, added_sites);
                if (!aln->site_state_freq.empty() && getNPattern() > nptn) {
                    // a new pattern is added, copy state frequency vector
                    double *state_freq = new double[num_states];
                    memcpy(state_freq, aln->site_state_freq[ptn_id],
                           num_states*sizeof(double));
                    site_state_freq.push_back(state_freq);
                }
                if (pattern_freq) ((*pattern_freq)[ptn_id])++;
                added_sites++;
            }
        }
        if (added_sites < nsite) {
            site_pattern.resize(added_sites);
        }
    } else if (strncmp(spec, "GENESITE,", 9) == 0) {
		// resampling genes, then resampling sites within resampled genes
		convert_int_vec(spec+9, site_vec);
		IntVector begin_site;
        intptr_t site = 0;
		for (int i = 0; i < static_cast<int>(site_vec.size()); ++i) {
			begin_site.push_back(static_cast<int>(site));
			site += site_vec[i];
			//cout << "site = " << site_vec[i] << endl;
		}
        if ( site > nsite ) {
            outError("Sum of lengths exceeded alignment length");
        }
		for (int i = 0; i < static_cast<int>(site_vec.size()); ++i) {
			int part = random_int(static_cast<int>(site_vec.size()));
			for (int j = 0; j < site_vec[part]; ++j) {
				site = random_int(static_cast<int>(site_vec[part])) + begin_site[part];
				int ptn = aln->getPatternID(site);
				Pattern pat = aln->at(ptn);
				addPattern(pat, static_cast<int>(site));
				if (pattern_freq) ((*pattern_freq)[ptn])++;
			}
		}
    } else if (strncmp(spec, "GENE,", 5) == 0) {
		// resampling genes instead of sites
		convert_int_vec(spec+5, site_vec);
        int site = 0;
		IntVector begin_site;
		for (int i = 0; i < static_cast<int>(site_vec.size()); ++i) {
			begin_site.push_back(site);
			site += site_vec[i];
			//cout << "site = " << site_vec[i] << endl;
		}
        if (site > getNSite32()) {
            outError("Sum of lengths exceeded alignment length");
        }
        for (int i = 0; i < static_cast<int>(site_vec.size()); ++i) {
            int part = random_int(static_cast<int>(site_vec.size()));
            for (site = begin_site[part];
                 site < begin_site[part] + site_vec[part]; site++) {
                int ptn = aln->getPatternID(site);
                Pattern pat = aln->at(ptn);
                addPattern(pat, site);
                if (pattern_freq) ((*pattern_freq)[ptn])++;
            }
        }
    } else {
    	// special bootstrap
    	convert_int_vec(spec, site_vec);
    	if (site_vec.size() % 2 != 0) {
    		outError("Bootstrap specification length is not divisible by 2");
        }
    	nsite = 0;
    	int begin_site = 0, out_site = 0;
    	for (size_t part = 0; part < site_vec.size(); part+=2) {
    		nsite += site_vec[part+1];
        }
    	site_pattern.resize(nsite, -1);
    	for (size_t part = 0; part < site_vec.size(); part+=2) {
    		if (begin_site + site_vec[part] > aln->getNSite()) {
    			outError("Sum of lengths exceeded alignment length");
            }
    		for (size_t site = 0; site < site_vec[part+1]; ++site) {
    			int site_id = random_int(site_vec[part]) + begin_site;
    			int ptn_id = aln->getPatternID(site_id);
    			Pattern pat = aln->at(ptn_id);
    			addPattern(pat, static_cast<int>(site + out_site));
    			if (pattern_freq) {
                    ((*pattern_freq)[ptn_id])++;
                }
    		}
    		begin_site += site_vec[part];
    		out_site += site_vec[part+1];
    	}
    }
    if (!aln->site_state_freq.empty()) {
        site_model = site_pattern;
        ASSERT(site_state_freq.size() == getNPattern());
    }
    verbose_mode = save_mode;
    countConstSite();
//    buildSeqStates();
}

void Alignment::createBootstrapAlignment(IntVector &pattern_freq,
                                         const char *spec) {
	size_t nptn = getNPattern();
    pattern_freq.resize(nptn, 0);
    int *internal_freq = new int [nptn];
    createBootstrapAlignment(internal_freq, spec);
    for (size_t i = 0; i < nptn; i++) {
        pattern_freq[i] = internal_freq[i];
    }
    delete [] internal_freq;
}

void Alignment::createBootstrapAlignment(int *pattern_freq,
                                         const char *spec,
                                         int *rstream) {
    intptr_t nsite = getNSite();
    memset(pattern_freq, 0, getNPattern()*sizeof(int));
	IntVector site_vec;
    if (Params::getInstance().jackknife_prop > 0.0 && spec) {
        outError((string)"Unsupported jackknife with " + spec);
    }

    if (spec && strncmp(spec, "SCALE=", 6) == 0) {
        // multi-scale bootstrapping called by AU test
        intptr_t orig_nsite = nsite;
        double scale = convert_double(spec+6);
        nsite = (size_t)round(scale * nsite);
        for (intptr_t site = 0; site < nsite; site++) {
            int site_id = random_int(static_cast<int>(orig_nsite), rstream);
            int ptn_id  = getPatternID(site_id);
            pattern_freq[ptn_id]++;
        }
    } else if (!spec) {

        intptr_t nptn = getNPattern();
        if (nsite/8 < nptn || Params::getInstance().jackknife_prop > 0.0) {
            IntVector sample;
            ASSERT(nsite < std::numeric_limits<int>::max());
            random_resampling(static_cast<int>(nsite), sample, rstream);
            for (intptr_t site = 0; site < nsite; site++) {
                for (int rep = 0; rep < sample[site]; rep++) {
                    int ptn_id = getPatternID(site);
                    pattern_freq[ptn_id]++;
                }
            }
        } else {
            // BQM 2015-12-27: use multinomial sampling for
            // faster generation if #sites is much larger than #patterns
            double *prob = new double[nptn];
            for (intptr_t ptn = 0; ptn < nptn; ++ptn) {
                prob[ptn] = at(ptn).frequency;
            }
            ASSERT(nsite < std::numeric_limits<unsigned int>::max());
            gsl_ran_multinomial(static_cast<const size_t>(nptn),
                                static_cast<const unsigned int>(nsite), prob, 
                                reinterpret_cast<unsigned int*>(pattern_freq), 
                                rstream);
            int sum = 0;
            for (intptr_t ptn = 0; ptn < nptn; ++ptn) {
                sum += pattern_freq[ptn];
            }
            ASSERT(sum == nsite);
            delete [] prob;
        }
    } else if (strncmp(spec, "GENESITE,", 9) == 0) {
		// resampling genes, then resampling sites within resampled genes
		convert_int_vec(spec+9, site_vec);
		IntVector begin_site;
        int site = 0;
        int site_count = static_cast<int>(site_vec.size());
        for (int i = 0; i < site_count; ++i) {
			begin_site.push_back(site);
			site += site_vec[i];
		}
        if (site > getNSite()) {
			outError("Sum of lengths exceeded alignment length");
        }
		for (int i = 0; i < site_count; ++i) {
			int part = random_int(site_count, rstream);
			for (int j = 0; j < site_vec[part]; j++) {
				site = random_int(site_vec[part], rstream) + begin_site[part];
				int ptn = getPatternID(site);
				pattern_freq[ptn]++;
			}
		}
	} else if (strncmp(spec, "GENE,", 5) == 0) {
		// resampling genes instead of sites
		convert_int_vec(spec+5, site_vec);
		IntVector begin_site;
        size_t site = 0;
		for (size_t i = 0; i < site_vec.size(); ++i) {
			begin_site.emplace_back(static_cast<int>(site));
			site += site_vec[i];
			//cout << "site = " << site_vec[i] << endl;
		}
        if (site > getNSite()) {
            outError("Sum of lengths exceeded alignment length");
        }
        int part_count = static_cast<int>(site_vec.size());
        for (int i = 0; i < part_count; ++i) {
            int part = random_int(part_count, rstream);
            for (site = begin_site[part];
                 site < begin_site[part] + site_vec[part]; ++site) {
                int ptn = getPatternID(site);
                pattern_freq[ptn]++;
            }
        }
    } else {
        // resampling sites within genes
        try {
            convert_int_vec(spec, site_vec);
        } catch (...) {
            outError("-bsam not allowed for non-partition model");
        }
		if (site_vec.size() % 2 != 0) {
			outError("Bootstrap specification length is not divisible by 2");
        }
		int begin_site = 0, out_site = 0;
		for (size_t part = 0; part < site_vec.size(); part += 2) {
			if (begin_site + site_vec[part] > getNSite()) {
				outError("Sum of lengths exceeded alignment length");
            }
			for (size_t site = 0; site < site_vec[part+1]; ++site) {
				int site_id = random_int(site_vec[part], rstream) + begin_site;
				int ptn_id = getPatternID(site_id);
				pattern_freq[ptn_id]++;
			}
			begin_site += site_vec[part];
			out_site += site_vec[part+1];
		}
    }
}


void Alignment::buildFromPatternFreq(Alignment & aln, IntVector new_pattern_freqs){
	size_t nsite = aln.getNSite();
    seq_names.insert(seq_names.begin(),
                     aln.seq_names.begin(), aln.seq_names.end());
    name = aln.name;
    model_name = aln.model_name;
    sequence_type = aln.sequence_type;
    position_spec = aln.position_spec;
    aln_file = aln.aln_file;
    num_states = aln.num_states;
    seq_type = aln.seq_type;

    genetic_code = aln.genetic_code;
    STATE_UNKNOWN = aln.STATE_UNKNOWN;
    site_pattern.resize(nsite, -1);

    clear();
    pattern_index.clear();

    int site = 0;
    std::vector<Pattern>::iterator it;
    int p;

    for(it = aln.begin(), p = 0; it != aln.end(); ++it, ++p) {
    	if(new_pattern_freqs[p] > 0){
	    	Pattern pat = *it;
			addPattern(pat, site, new_pattern_freqs[p]);
			for (int j = 0; j < new_pattern_freqs[p]; j++) {
				site_pattern[site++] = static_cast<int>(size())-1;
            }
    	}
    }
    if (!aln.site_state_freq.empty()) {
        site_model = site_pattern;
        ASSERT(site_state_freq.size() == getNPattern());
    }

    countConstSite();
//    buildSeqStates();
//    checkSeqName();
}


void Alignment::createGapMaskedAlignment(Alignment *masked_aln, Alignment *aln) {
    if (masked_aln->getNSeq() != aln->getNSeq()) {
        outError("Different number of sequences in masked alignment");
    }
    if (masked_aln->getNSite() != aln->getNSite()) {
        outError("Different number of sites in masked alignment");
    }
    intptr_t nsite = aln->getNSite();
    intptr_t nseq  = aln->getNSeq();
    seq_names.insert(seq_names.begin(),
                     aln->seq_names.begin(), aln->seq_names.end());
    name = aln->name;
    model_name = aln->model_name;
    position_spec = aln->position_spec;
    aln_file = aln->aln_file;
    copyStateInfoFrom(aln);
    site_pattern.resize(nsite, -1);
    clear();
    pattern_index.clear();
    IntVector name_map;
    for (auto it = seq_names.begin(); it != seq_names.end(); it++) {
        intptr_t seq_id = masked_aln->getSeqID(*it);
        if (seq_id < 0) {
            outError("Masked alignment does not contain taxon ", *it);
        }
        name_map.push_back(static_cast<int>(seq_id));
    }
    VerboseMode save_mode = verbose_mode;
    verbose_mode = min(verbose_mode, VerboseMode::VB_MIN);
    // to avoid printing gappy sites in addPattern
    for (intptr_t site = 0; site < nsite; ++site) {
        int ptn_id = aln->getPatternID(site);
        Pattern pat = aln->at(ptn_id);
        Pattern masked_pat = masked_aln->at(masked_aln->getPatternID(site));
        for (intptr_t seq = 0; seq < nseq; ++seq) {
            if (masked_pat[name_map[seq]] == STATE_UNKNOWN) {
                pat[seq] = STATE_UNKNOWN;
            }
        }
        addPattern(pat, static_cast<int>(site));
    }
    verbose_mode = save_mode;
    countConstSite();
}

void Alignment::shuffleAlignment() {
    if (isSuperAlignment()) outError("Internal error: ", __func__);
    my_random_shuffle(site_pattern.begin(), site_pattern.end());
}


void Alignment::concatenateAlignment(Alignment *aln) {
    if (getNSeq() != aln->getNSeq()) {
        outError("Different number of sequences in two alignments");
    }
    if (num_states != aln->num_states) {
        outError("Different number of states in two alignments");
    }
    if (seq_type != aln->seq_type) {
        outError("Different data type in two alignments");
    }
    intptr_t nsite     = aln->getNSite();
    size_t   cur_sites = getNSite();
    site_pattern.resize(cur_sites + nsite , -1);
    IntVector name_map;
    for (auto it = seq_names.begin(); it != seq_names.end(); it++) {
        intptr_t seq_id = aln->getSeqID(*it);
        if (seq_id < 0) {
            outError("The other alignment does not contain taxon ", *it);
        }
        name_map.push_back(static_cast<int>(seq_id));
    }
    VerboseMode save_mode = verbose_mode;
    verbose_mode = min(verbose_mode, VerboseMode::VB_MIN);
    // to avoid printing gappy sites in addPattern
    for (intptr_t site = 0; site < nsite; ++site) {
        Pattern pat = aln->at(aln->getPatternID(site));
        Pattern new_pat = pat;
        for (size_t i = 0; i < name_map.size(); i++) {
            new_pat[i] = pat[name_map[i]];
        }
        addPattern(new_pat, static_cast<int>(site + cur_sites));
    }
    verbose_mode = save_mode;
    countConstSite();
}

void Alignment::copyStateInfoFrom(const Alignment* aln) {
    sequence_type  = aln->sequence_type;
    seq_type       = aln->seq_type;
    num_states     = aln->num_states;
    STATE_UNKNOWN  = aln->STATE_UNKNOWN;
    genetic_code   = aln->genetic_code;
    codon_table    = aln->codon_table;
    non_stop_codon = aln->non_stop_codon;
}

void Alignment::copyAlignment(Alignment *aln) {
    size_t nsite = aln->getNSite();
    seq_names.insert(seq_names.begin(),
                     aln->seq_names.begin(), aln->seq_names.end());
    name           = aln->name;
    model_name     = aln->model_name;
    position_spec  = aln->position_spec;
    aln_file       = aln->aln_file;
    copyStateInfoFrom(aln);
    site_pattern.resize(nsite, -1);
    clear();
    pattern_index.clear();
    VerboseMode save_mode = verbose_mode;
    verbose_mode = min(verbose_mode, VerboseMode::VB_MIN);
    // to avoid printing gappy sites in addPattern
    for (size_t site = 0; site < nsite; ++site) {
        intptr_t site_id = site;
        intptr_t ptn_id = aln->getPatternID(site_id);
        Pattern pat = aln->at(ptn_id);
        addPattern(pat, static_cast<int>(site));
    }
    verbose_mode = save_mode;
    countConstSite();
}

bool Alignment::isCompatible(const Alignment* other, std::string& whyNot) const {
    std::stringstream reason;
    if (seq_type!=other->seq_type) {
        reason << "Sequence type (" << other->sequence_type << ")"
        << " disagrees in file " << other->aln_file << "\n";
    }
    if (num_states!=other->num_states) {
        reason << "Number of states (" << other->num_states << ")"
        << " disagrees in file " << other->aln_file << "\n";
    }
    if (STATE_UNKNOWN!=other->STATE_UNKNOWN) {
        reason << "Unknown state (" << other->STATE_UNKNOWN << ")"
        << " disagrees in file " << other->aln_file << "\n";
    }
    if (getNSite()!=other->getNSite()) {
        reason << "Number of sites (" << other->getNSite() << ")"
        << " disagrees in file " << other->aln_file << "\n";
    }
    whyNot = reason.str();
    return whyNot.empty();
}

bool Alignment::updateFrom(const Alignment* other,
                           const std::vector<std::pair<int,int>>& updated_sequences,
                           const IntVector& added_sequences,
                           progress_display_ptr progress) {
    std::string why_not;
    if (!isCompatible(other, why_not)) {
        return false;
    }
    StrVector sequences;
    getAllSequences("", sequences);
    StrVector stateStrings;
    getStateStrings(stateStrings);
    
    IntVector dest_seq_ids;
    IntVector source_seq_ids;
    for (auto it=updated_sequences.begin();
         it!=updated_sequences.end(); ++it) {
        dest_seq_ids.push_back(it->first);
        source_seq_ids.push_back(it->second);
    }
    intptr_t update_count = dest_seq_ids.size();
        
    #ifdef _OPENMP
    #pragma omp parallel for
    #endif
    for (intptr_t i=0; i<update_count; ++i) {
        size_t source_id = source_seq_ids[i];
        size_t dest_id   = dest_seq_ids[i];
        std::string replacement;
        other->getOneSequence(stateStrings, source_id, replacement);
        sequences[dest_id] = replacement;
        if (progress!=nullptr) {
            (*progress) += 1.0;
        }
    }
    
    intptr_t old_seq_count = sequences.size();
    intptr_t add_count     = added_sequences.size();
    intptr_t nseq          = old_seq_count + add_count;
    sequences.resize(nseq);
    seq_names.resize(nseq);
    #ifdef _OPENMP
    #pragma omp parallel for
    #endif
    for (intptr_t r=0; r<add_count; ++r) {
        int    other_seq_id = added_sequences[r];
        size_t w            = old_seq_count + r;
        seq_names[w]        = other->getSeqName(other_seq_id);
        other->getOneSequence(stateStrings, other_seq_id, sequences[w] );
        if (progress!=nullptr) {
            (*progress) = 1.0;
        }
    }
    int  nsite = getNSite32();
    ASSERT(nseq < std::numeric_limits<int>::max());
    bool rc    = constructPatterns(static_cast<int>(nseq),
                                   nsite, sequences, progress);
    orderPatternByNumChars(PAT_VARIANT);
    return rc;
}


void Alignment::countConstSite()  {
    int num_const_sites = 0;
    num_informative_sites = 0;
    num_variant_sites = 0;
    int num_invariant_sites = 0;
    num_parsimony_sites = 0;
    for (iterator it = begin(); it != end(); ++it) {
        if ((*it).isConst())
            num_const_sites += (*it).frequency;
        if (it->isInformative())
            num_informative_sites += it->frequency;
        if (it->isInvariant())
            num_invariant_sites += it->frequency;
        else
            num_variant_sites += it->frequency;
    }
    frac_const_sites = ((double)num_const_sites) / getNSite();
    frac_invariant_sites = ((double)num_invariant_sites) / getNSite();
}

/**
 * generate all subsets of a set
 * @param inset input set
 * @param[out] subsets vector of all subsets of inset
 */
template<class T>
void generateSubsets(vector<T> &inset, vector<vector<T> > &subsets) {
    if (inset.size() > 30) {
        outError("Cannot work with more than 31 states");
    }
    uint64_t total = ((uint64_t)1 << inset.size());
    for (uint64_t binrep = 0; binrep < total; binrep++) {
        vector<T> subset;
        for (uint64_t i = 0; i < inset.size(); i++)
            if (binrep & ((uint64_t)1 << i))
                subset.push_back(inset[i]);
        subsets.push_back(subset);
    }
}

void Alignment::generateUninfPatterns(StateType repeat,
                                      vector<StateType> &singleton,
                                      vector<int> &seq_pos,
                                      vector<Pattern> &unobserved_ptns) {
    intptr_t seqs = getNSeq();
    intptr_t seq_pos_size = seq_pos.size();
    if (seq_pos_size == singleton.size()) {
        Pattern pat;
        pat.resize(seqs, repeat);
        for (intptr_t i = 0; i < seq_pos_size; i++) {
            pat[seq_pos[i]] = singleton[i];
        }
        unobserved_ptns.push_back(pat);
        return;
    }
    for (intptr_t seq = 0; seq < seqs; seq++) {
        bool dup = false;
        for (auto s: seq_pos) {
            if (seq == s) { 
                dup = true; 
                break; 
            }
        }
        if (dup) {
            continue;
        }
        vector<int> seq_pos_new = seq_pos;
        seq_pos_new.push_back(static_cast<int>(seq));
        generateUninfPatterns(repeat, singleton, seq_pos_new, unobserved_ptns);
    }
}

void Alignment::getUnobservedConstPatterns(ASCType ASC_type,
                                           vector<Pattern>& unobserved_ptns) {
    switch (ASC_type) {
        case ASCType::ASC_NONE: 
            break;
        case ASCType::ASC_VARIANT: 
            getUnobservedConstPatternsLewis(unobserved_ptns);
            break;
        case ASCType::ASC_VARIANT_MISSING:
            getUnobservedConstPatternsHolder(unobserved_ptns);
            break;
        case ASCType::ASC_INFORMATIVE: 
            getUnobservedConstPatternsHolderForInformativeSites(unobserved_ptns);
            break;
        case ASCType::ASC_INFORMATIVE_MISSING: 
            // Holder correction for informative sites with missing data
            ASSERT(0 && "Not supported yet");
            break;
    }
}

void Alignment::getUnobservedConstPatternsLewis(vector<Pattern>& unobserved_ptns) {
    // Lewis's correction for variant sites
    unobserved_ptns.reserve(num_states);
    for (StateType state = 0; state < (StateType)num_states; state++) {
        if (!isStopCodon(state)) {
            Pattern pat;
            pat.resize(getNSeq(), state);
            if (pattern_index.find(pat) == pattern_index.end()) {
                // constant pattern is unobserved
                unobserved_ptns.push_back(pat);
            }
        }
    }    
}

void Alignment::getUnobservedConstPatternsHolder(vector<Pattern>& unobserved_ptns) {
    // Holder's correction for variant sites with missing data
    intptr_t orig_nptn     = getNPattern();
    intptr_t max_orig_nptn = get_safe_upper_limit(orig_nptn);
    unobserved_ptns.reserve(max_orig_nptn*num_states);
    intptr_t nseq = getNSeq();
    for (StateType state = 0; static_cast<int>(state) < num_states; state++) {
        for (intptr_t ptn = 0; ptn < max_orig_nptn; ptn++) {
            Pattern new_ptn;
            if (ptn < orig_nptn) {
                new_ptn.reserve(nseq);
                for (auto state_ptn: at(ptn)) {
                    if (static_cast<int>(state_ptn) < num_states) {
                        new_ptn.push_back(state);
                    }
                    else {
                        new_ptn.push_back(STATE_UNKNOWN);
                    }
                }
            } 
            else {
                new_ptn.resize(nseq, STATE_UNKNOWN);
            }
            unobserved_ptns.push_back(new_ptn);
        }
    }
}

void Alignment::getUnobservedConstPatternsHolderForInformativeSites
        (vector<Pattern>& unobserved_ptns) {
    // Holder correction for informative sites
    for (StateType repeat = 0; static_cast<int>(repeat) < num_states; repeat++) {
        vector<StateType> rest;
        rest.reserve(num_states-1);
        for (StateType s = 0; static_cast<int>(s) < num_states; s++) {
            if (s != repeat) {
                rest.push_back(s);
            }
        }
        vector<vector<StateType> > singletons;
        generateSubsets(rest, singletons);
        for (vector<StateType>& singleton : singletons) {
            intptr_t singleton_count = singletons.size();
            if (singleton_count < getNSeq() - 1 ||
                (singleton_count == getNSeq() - 1 && repeat == 0)) {
                vector<int> seq_pos;
                generateUninfPatterns(repeat, singleton,
                    seq_pos, unobserved_ptns);
            }
        }
    }
}

int Alignment::countProperChar(int seq_id) const {
    int num_proper_chars = 0;
    for (auto it = begin(); it != end(); ++it) {
        if ((*it)[seq_id] < num_states + pomo_sampled_states.size()) {
            num_proper_chars+=(*it).frequency;
        }
    }
    return num_proper_chars;
}

Alignment::~Alignment()
{
    delete [] pars_lower_bound;
    pars_lower_bound = nullptr;
    for (auto it = site_state_freq.rbegin();
         it != site_state_freq.rend(); ++it) {
        delete [] (*it);
    }
    site_state_freq.clear();
    site_model.clear();
}

double Alignment::computeObsDist(int seq1, int seq2) const {
    size_t diff_pos = 0;
    size_t total_pos = getNSite() - num_variant_sites;
    // initialize with number of constant sites
    for (auto it = begin(); it != end(); ++it) {
        if ((*it).isConst()) {
            continue;
        }
        int state1 = convertPomoState((*it)[seq1]);
        int state2 = convertPomoState((*it)[seq2]);
        if  (state1 < num_states && state2 < num_states) {
            total_pos += (*it).frequency;
            if (state1 != state2 ) {
                diff_pos += (*it).frequency;
            }
        }
    }
    if (!total_pos) {
        if (verbose_mode >= VerboseMode::VB_MED)
        {
            outWarning("No overlapping characters between "
                       + getSeqName(seq1) + " and " + getSeqName(seq2));
        }
        return MAX_GENETIC_DIST; // return +INF if no overlap between two sequences
    }
    return ((double)diff_pos) / total_pos;
}

double Alignment::computeJCDistanceFromObservedDistance(double obs_dist) const
{
    double z = (double)num_states / (num_states-1);
    double x = 1.0 - (z * obs_dist);
    if (x <= 0) {
        return MAX_GENETIC_DIST;
    }
    return -log(x) / z;
}

double Alignment::computeJCDist(int seq1, int seq2) const {
    double obs_dist = computeObsDist(seq1, seq2);
    return computeJCDistanceFromObservedDistance(obs_dist);
}

template <class S> void Alignment::printDist(const std::string& format,
                                             S &out, double *dist_mat) const {
    intptr_t nseqs   = getNSeq();
    size_t   max_len = getMaxSeqNameLength();
    if (max_len < 10) max_len = 10;
    out << nseqs << endl;
    auto precision = max((int)ceil(-log10(Params::getInstance().min_branch_length))+1, 6);
    out.precision(precision);
    bool lower = (format.substr(0,5) == "lower");
    bool upper = (format.substr(0,5) == "upper");
    for (intptr_t seq1 = 0; seq1 < nseqs; ++seq1)  {
        std::stringstream line;
        line.width(max_len);
        line.precision(precision);
        line << std::fixed << std::left << getSeqName(seq1);
        intptr_t rowStart = upper ? (seq1+1) : 0;
        intptr_t rowStop  = lower ? (seq1) : nseqs;
        intptr_t pos      = seq1 * nseqs + rowStart;
        for (intptr_t seq2 = rowStart; seq2 < rowStop; ++seq2) {
            line << " ";
            line << dist_mat[pos++];
        }
        line << "\n";
        out << line.str();
    }
    out.flush();
}

void Alignment::printDist ( const std::string& format, int compression_level
                           , const char *file_name, double *dist_mat) const {
    try {
        if (!contains(format,"gz")) {
            ofstream out;
            out.exceptions(ios::failbit | ios::badbit);
            out.open(file_name);
            printDist(format, out, dist_mat);
            out.close();
        } else {
            //Todo: Decide. Should we be insisting
            //      the file name ends with .gz too?
            ogzstream out;
            out.exceptions(ios::failbit | ios::badbit);
            out.open(file_name, ios::out, compression_level);
            printDist(format, out, dist_mat);
            out.close();
        }
        //cout << "Distance matrix was printed to " << file_name << endl;
    } catch (ios::failure) {
        outError(ERR_WRITE_OUTPUT, file_name);
    }
}

double Alignment::readDist(igzstream &in, bool is_incremental,
                           double *dist_mat) {
    double longest_dist = 0.0;
    std::stringstream firstLine;
    safeGetTrimmedLineAsStream(in, firstLine);
    intptr_t nseqs; //Number of sequences in distance matrix file
    firstLine >> nseqs;
    if (!is_incremental && nseqs != getNSeq()) {
        throw "Distance file has different number of taxa";
    }
    DoubleVector temporary_distance_matrix;
    temporary_distance_matrix.resize(nseqs * nseqs, 0);
    double *tmp_dist_mat = temporary_distance_matrix.data();
    
    bool lower = false; //becomes true if lower-triangle format identified
    bool upper = false; //becomes true if upper-triangle format identified
    std::map< string, intptr_t> map_seqName_ID;
    #if USE_PROGRESS_DISPLAY
    progress_display readProgress(nseqs*nseqs,
                                  "Reading distance matrix from file");
    #else
    double readProgress = 0.0;
    #endif
    // read in distances to a temporary array
        
    for (intptr_t seq1 = 0; seq1 < nseqs; seq1++)  {
        readDistLine(in, nseqs, seq1, upper, lower, longest_dist, 
                     tmp_dist_mat, map_seqName_ID, readProgress);
    }
    
    if (lower) {
        //Copy the upper triangle of the matrix from the lower triangle
        for (intptr_t seq1=0; seq1<nseqs; ++seq1) /*row*/ {
            intptr_t rowStart = seq1     * nseqs; //start of (seq1)th row (in dist matrix)
            intptr_t rowStop  = rowStart + nseqs; //end of (seq1)th row; start of (seq1+1)th.
            intptr_t colPos   = rowStop  + seq1;  //(seq1)th column of (seq1+1)th row
            //Run across the row, writing the [seq1+1]th through [nseq-1]th
            //elements of the row, with values read down the (seq1)th column
            //of the lower triangle.
            for (intptr_t rowPos=rowStart+seq1+1; rowPos<rowStop; ++rowPos, colPos+=nseqs) {
                tmp_dist_mat[rowPos] = tmp_dist_mat[colPos];
            }
        }
    }
    #if USE_PROGRESS_DISPLAY
    readProgress.done();
    #endif
    
    {
        // Now initialize the internal distance matrix, in which
        // the sequence order is the same as in the alignment
        std::vector<intptr_t> actual_to_temp_vector;
        actual_to_temp_vector.resize(nseqs, 0);
        intptr_t* actualToTemp = actual_to_temp_vector.data();

        mapLoadedSequencesToAlignment(map_seqName_ID, nseqs, 
                                    is_incremental, actualToTemp);

        copyToDistanceMatrix(tmp_dist_mat, nseqs, actualToTemp, dist_mat);
    }
    temporary_distance_matrix.clear();
    tmp_dist_mat = nullptr;

    checkForSymmetricMatrix(dist_mat, nseqs);

    /*
    string dist_file = params.out_prefix;
    dist_file += ".userdist";
    printDist(params.dist_format, dist_file.c_str(), dist_mat);*/

    return longest_dist;
}

void Alignment::readDistLine(igzstream &in, intptr_t nseqs, intptr_t seq1,
                             bool &upper, bool &lower, double& longest_dist,
                             double *tmp_dist_mat,  
                             std::map<string,intptr_t>& map_seqName_ID,
                             progress_display& readProgress) {
    std::stringstream line;
    safeGetTrimmedLineAsStream(in, line);
    
    string seq_name;
    line >> seq_name;
    if (map_seqName_ID.find(seq_name) != map_seqName_ID.end()) {
        //When reading a distance file "incrementally", we can't tolerate
        //duplicate sequence names (and we won't detect them later).
        //Formerly duplicate sequence names weren't found early, here.
        //Rather, they were left as is, and a later check (that every
        //sequence name in the alignment had a matching line in the distance
        //file) reported a problem (not the *right* problem, but a problem).
        //But when is_incremental is true, that later check won't throw.
        stringstream s;
        s   << "Duplicate sequence name found in line " << (seq1+1)
            << " of the file: " << seq_name;
        throw s.str();
    }
    // assign taxa name to integer id
    map_seqName_ID[seq_name] = static_cast<int>(seq1);

    size_t pos = nseqs * seq1;
    if (upper) {
        //Copy column seq1 from upper triangle (above the diagonal)
        //to the left part of row seq1 (which is in the lower triangle).
        intptr_t column_pos = seq1; //position in column in upper triangle
        for (intptr_t seq2=0; seq2<seq1; ++seq2, column_pos+=nseqs) {
            tmp_dist_mat[pos++] = tmp_dist_mat[column_pos];
        }
        //And write zero on the diagonal, in row seq1.
        tmp_dist_mat[pos++] = 0.0;
    }
    intptr_t rowStart = (upper) ? (seq1+1) : 0; //(row-start relative)
    intptr_t rowStop  = (lower) ? seq1     : nseqs;
    intptr_t seq2     = rowStart;
    for (; line.tellg()!=-1 && seq2 < rowStop; ++seq2) {
        double dist;
        line >> dist;
        tmp_dist_mat[pos++] = dist;
        if (dist > longest_dist) {
            longest_dist = dist;
        }
    }
    if (line.tellg()==-1 && seq2<rowStop)
    {
        readShortDistLine(seq_name, seq1, seq2, rowStop, 
                          upper, lower, 
                          tmp_dist_mat, readProgress);
    }
    else if (lower) {
        tmp_dist_mat[pos++] = 0.0; //Diagonal entry
        //Leave cells in upper triangle empty (for now)
    }
    readProgress += (rowStop - rowStart) * ((lower||upper) ? 2.0 : 1.0);
}

void Alignment::readShortDistLine(const std::string& seq_name, 
                                  intptr_t seq1, intptr_t seq2, intptr_t rowStop,
                                  bool& upper, bool& lower, double *tmp_dist_mat,
                                  progress_display& readProgress) {
    if (seq1==0 && seq2==0) {
        //Implied lower-triangle format
        tmp_dist_mat[0] = 0.0;
        if (verbose_mode >= VerboseMode::VB_MED) {
            #if USE_PROGRESS_DISPLAY
            readProgress.hide();
            std::cout << "Distance matrix file "
                        << " is in lower-triangle format" << std::endl;
            readProgress.show();
            #endif
        }
        lower  = true;
    }
    else if (seq1==0 && seq2+1==rowStop) {
        if (verbose_mode >= VerboseMode::VB_MED) {
            #if USE_PROGRESS_DISPLAY
            readProgress.hide();
            std::cout << "Distance matrix file "
                        << " is in upper-triangle format" << std::endl;
            readProgress.show();
            #endif
        }
        upper  = true;
        for (; 0<seq2; --seq2) {
            tmp_dist_mat[seq2] = tmp_dist_mat[seq2-1];
        }
        tmp_dist_mat[0] = 0.0;
    }
    else {
        std::stringstream problem;
        problem << "Too few distances read from row " << (seq1+1)
                << " of the distance matrix, for sequence " << seq_name;
        throw problem.str();
    }
}

void Alignment::mapLoadedSequencesToAlignment(std::map<string,intptr_t>& map_seqName_ID,
                                              intptr_t nseqs, bool is_incremental, 
                                              intptr_t* actualToTemp) {
    size_t missingSequences = 0; //count of sequences missing from temporary matrix                                                
    for (intptr_t seq1 = 0; seq1 < nseqs; ++seq1) {
        string   seq1Name    = getSeqName(seq1);
        intptr_t seq1_tmp_id = -1;
        if (map_seqName_ID.count(seq1Name) == 0) {
            if (is_incremental) {
                ++missingSequences;
            } else {
                throw "Could not find taxa name " + seq1Name;
            }
        } else {
            seq1_tmp_id = map_seqName_ID[seq1Name];
        }
        actualToTemp[seq1] = seq1_tmp_id;
    }
    if (is_incremental) {
        if ( 0 < missingSequences || nseqs != getNSeq() ) {
            std::cout << missingSequences << " sequences have been added, "
                << (nseqs + missingSequences - getNSeq() ) //Always >=0
                << " sequences (found in the distance file) have been removed."
                << std::endl;
        }
    }
    std::cout << std::endl;
}

void Alignment::copyToDistanceMatrix(double*   tmp_dist_mat, intptr_t nseqs,
                                     intptr_t* actualToTemp, double*  dist_mat) {

    //Copy distances from tmp_dist_mat to dist_mat,
    //permuting rows and columns on the way
    //(by looking up row and column numbers, in the
    //temporary distance matrix, via actualToTemp).
    //(and, in incremental mode, write zeroes into
    //"missing" rows or columns (ones that don't
    //have counterparts in the temporary distance matrix)
    #ifdef _OPENMP
    #pragma omp parallel for
    #endif
    for (intptr_t seq1 = 0; seq1 < nseqs; seq1++) {
        auto writeRow = dist_mat + seq1 * nseqs;
        if ( 0 <= actualToTemp[seq1] ) {
            auto readRow  = tmp_dist_mat + actualToTemp[seq1] * nseqs;
            for (intptr_t seq2 = 0; seq2 < nseqs; seq2++) {
                if ( 0 <= actualToTemp[seq2] ) {
                    writeRow[ seq2 ] = readRow [ actualToTemp[seq2] ];
                } else {
                    writeRow[ seq2 ] = 0.0; //zero in
                }
            }
        } else {
            for (intptr_t seq2 = 0; seq2 < nseqs; seq2++) {
                writeRow[ seq2 ] = 0.0;
            }
        }
    }
    
    /*
    pos = 0;
    for (size_t seq1 = 0; seq1 < nseqs; seq1++) {
        for (size_t seq2 = 0; seq2 < nseqs; seq2++) {
            std::cout << " " << dist_mat[pos++];
        }
        std::cout << std::endl;
    }
    */
}

void Alignment::checkForSymmetricMatrix(double* dist_mat, intptr_t nseqs) {
    for (intptr_t seq1 = 0; seq1+1 < nseqs; seq1++) {
        auto checkRow = dist_mat + seq1*nseqs;
        if (checkRow[seq1] != 0.0) {
            throw "Diagonal elements of distance matrix is not ZERO";
        }
        auto checkColumn = checkRow + seq1 + nseqs; //same column of next row down
        for (intptr_t seq2 = seq1+1; seq2 < nseqs;
             seq2++, checkColumn+=nseqs) {
            if (checkRow[seq2] != *checkColumn) {
                std::stringstream problem;
                problem << "Distance between " << getSeqName(seq1)
                    << " and " << getSeqName(seq2)
                    << "( sequence ranks " << seq1
                    << " and " << seq2 << ")"
                    << " is not symmetric";
                throw problem.str();
            }
        }
    }
}

double Alignment::readDist(const char *file_name, bool is_incremental,
                           double *dist_mat) {
    double longest_dist = 0.0;

    try {
        igzstream in;
        in.exceptions(ios::failbit | ios::badbit);
        in.open(file_name);
        longest_dist = readDist(in, is_incremental, dist_mat);
        in.close();
        cout << "Distance matrix was read from " << file_name << endl;
    } catch (const char *str) {
        outError(str);
    } catch (string& error_str) {
        outError(error_str);
    } catch (ios::failure& io_fail) {
        outError(ERR_READ_INPUT, file_name);
    }
    return longest_dist;
}

void Alignment::countStatesForSites(size_t startPattern,
                                    size_t stopPattern,
                                    size_t *state_count) const {
    memset(state_count, 0, sizeof(size_t)*(STATE_UNKNOWN+1));
    for (size_t patternIndex = startPattern;
         patternIndex < stopPattern; ++patternIndex ) {
        const Pattern& pat = at(patternIndex);
        int   freq         = pat.frequency;
        const Pattern::value_type *stateArray = pat.data();
        size_t stateCount = pat.size();
        for (int i=0; i<stateCount; ++i) {
            int state = convertPomoState(stateArray[i]);
            if (state<0 || static_cast<int>(STATE_UNKNOWN)<state) {
                state = STATE_UNKNOWN;
            }
            state_count[state] += freq;
        }
    }
}

void Alignment::countStates(size_t *state_count, 
                            size_t num_unknown_states) const {
    //Note: this was suprisingly slow in Windows builds (Don't know why)
    memset(state_count, 0, sizeof(size_t)*(STATE_UNKNOWN+1));
    state_count[(int)STATE_UNKNOWN] = num_unknown_states;
#if defined(_OPENMP)
    int thread_count = omp_get_max_threads();
    intptr_t step    = ( size() + thread_count - 1 ) / thread_count;
    if (1<thread_count) {
        #pragma omp parallel for schedule(static,1)
        for (int thread=0; thread<thread_count; ++thread) {
            size_t start = thread * step;
            size_t stop  = start + step;
            if (size() < stop) {
                stop = size();
            }
#ifndef _MSC_VER
            size_t localStateCount[this->STATE_UNKNOWN+1];
#else
            boost::scoped_array<size_t> localStateCount(new size_t[this->STATE_UNKNOWN + 1]);
#endif
            countStatesForSites(start, stop, &localStateCount[0]);
            #pragma omp critical (sum_states)
            {
                for (int state=0; state<=static_cast<int>(STATE_UNKNOWN); ++state) {
                    state_count[state] += localStateCount[state];
                }
            }
        }
    } else
#endif
    {
        for (auto it = begin(); it != end(); ++it) {
            int freq = it->frequency;
            for (auto it2 = it->begin(); it2 != it->end(); ++it2) {
                int state = convertPomoState((int)*it2);
                if (state<0 || static_cast<int>(STATE_UNKNOWN)<state) {
                    state = STATE_UNKNOWN;
                }
                state_count[state] += freq;
            }
        }
    }
}

void Alignment::countStatesForSubset
        (const IntVector& subset,
         std::vector<size_t>& state_count) const {
    //It is assumed all of the indices in the subset are
    //valid.
    //
    //Todo: Multithread. Make this the single-threaded
    //version.  Write a multi-threaded version that divides
    //large subsets into pieces, and calls *this* version,
    //for each of the pieces of the subsets, then adds up
    //the answers that came back.
    //
    for (const Pattern& pat : *this ) {
        int freq = pat.frequency;
        for (int i : subset) {
            int state = convertPomoState(pat[i]);
            if (state<0 || static_cast<int>(STATE_UNKNOWN)<state) {
                state = STATE_UNKNOWN;
            }
            state_count[state] += freq;
        }
    }
    if (VerboseMode::VB_MAX <= verbose_mode ) {
        std::stringstream message;
        message << "State counts for subset were [ ";
        const char* sep = "";
        for ( auto count : state_count) {
            message << sep << count;
            sep = ", ";
        }
        message << " ].";
        std::cout << message.str() << std::endl;
    }
}

void Alignment::convertCountToFreq(size_t *state_count, double *state_freq) const {
    int i, j;
    double *states_app     = new double[num_states*(STATE_UNKNOWN+1)];
    double *new_freq       = new double[num_states];
    double *new_state_freq = new double[num_states];
    
    for (i = 0; i <= static_cast<int>(STATE_UNKNOWN); i++) {
        getAppearance(i, &states_app[i*num_states]);
    }
    for (i = 0; i < num_states; i++) {
        state_freq[i] = 1.0/num_states;
    }
    const int NUM_TIME = 8;
    for (int k = 0; k < NUM_TIME; k++) {
        memset(new_state_freq, 0, sizeof(double)*num_states);

        for (i = 0; i <= static_cast<int>(STATE_UNKNOWN); i++) {
            if (state_count[i] == 0) {
                continue;
            }
            double sum_freq = 0.0;
            for (j = 0; j < num_states; j++) {
                new_freq[j] = state_freq[j] * states_app[i*num_states+j];
                sum_freq += new_freq[j];
            }
            sum_freq = 1.0/sum_freq;
            for (j = 0; j < num_states; j++) {
                new_state_freq[j] += new_freq[j]*sum_freq*state_count[i];
            }
        }
        double sum_freq = 0.0;
        for (j = 0; j < num_states; j++) {
            sum_freq += new_state_freq[j];
        }
        if (sum_freq == 0.0) {
            break;
        }
        sum_freq = 1.0/sum_freq;
        for (j = 0; j < num_states; j++) {
            state_freq[j] = new_state_freq[j]*sum_freq;
        }
    }

    convfreq(state_freq);
    delete [] new_state_freq;
    delete [] new_freq;
    delete [] states_app;
}

// TODO DS: This only works when the sampling method is SAMPLING_SAMPLED or when
// the virtual population size is also the sample size (for every species and
// every site).
void Alignment::computeStateFreq (double *state_freq,
                                  size_t num_unknown_states,
                                  PhyloTree* report_to_tree) {
    std::vector<size_t> state_count_vector(STATE_UNKNOWN+1);
    size_t *state_count = state_count_vector.data();

    countStates(state_count, num_unknown_states);
    convertCountToFreq(state_count, state_freq);

    if (verbose_mode >= VerboseMode::VB_MED) {
        if (report_to_tree != nullptr) {
            report_to_tree->hideProgress();
        }
        cout << "Empirical state frequencies: ";
        cout << setprecision(10);
        for (int i = 0; i < num_states; i++) {
            cout << state_freq[i] << " ";
        }
        cout << endl;
        if (report_to_tree != nullptr) {
            report_to_tree->showProgress();
        }
    }
}

void Alignment::computeStateFreqForSubset
        (const IntVector& taxon_subset,
         double* state_freq) const {
    std::vector<size_t> state_count_vector(STATE_UNKNOWN+1, 0);
    countStatesForSubset(taxon_subset, state_count_vector);
    convertCountToFreq(state_count_vector.data(), state_freq);
}

int Alignment::convertPomoState(int state) const {
  // This map from an observed state to a PoMo state influences parsimony
  // construction and the +I likelihood computation. It should not make too much
  // of a difference though.

    if (seq_type != SeqType::SEQ_POMO) return state;
    if (state < num_states) return state;
    if (state == STATE_UNKNOWN) return state;
    state -= num_states;
    if (pomo_sampled_states.size() <= 0)
        outError("Alignment file is too short.");
    if (state >= pomo_sampled_states.size()) {
        cout << "state:              " << state << endl;
        cout << "pomo_sampled_states.size(): " << pomo_sampled_states.size() << endl;
    }
    ASSERT(state < pomo_sampled_states.size());
    int id1 = pomo_sampled_states[state] & 3;
    int id2 = (pomo_sampled_states[state] >> 16) & 3;
    int value1 = (pomo_sampled_states[state] >> 2) & 16383;
    int value2 = pomo_sampled_states[state] >> 18;
    int N = virtual_pop_size;
    int M = value1 + value2;

    // Mon Jun 13 13:24:55 CEST 2016. This is a stochastic way to assign PoMo
    // states. This is important if the sample size is small.

    // double stoch = (double) rand() / RAND_MAX - 0.5;
    // stoch /= 2.0;
    // int pick = (int)round(((double) value1*N/M) + stoch);

    // Fri Aug 18 15:37:22 BST 2017 However, Minh prefers a deterministic way
    // that, necessarily, introduces some systematic error.

    // BQM: Prefer the state with highest likelihood.

    // TODO: How to break tie? E.g., 4A4C but N=9? This way always prefers the
    // first allele (which is equivalent to a bias towards A and C, kind of).

    int pick = (int)round(((double) value1*N/M));

    int real_state;
    if (pick <= 0)
        real_state = id2;
    else if (pick >= N)
        real_state = id1;
    else {
        int j;
        if (id1 == 0) j = id2 - 1;
        else j = id1 + id2;
        real_state = 3 + j*(N-1) + pick;
    }
    state = real_state;
    ASSERT(state < num_states);
    return state;
}

void Alignment::computeAbsoluteStateFreq(unsigned int *abs_state_freq) {
    memset(abs_state_freq, 0, num_states * sizeof(unsigned int));

    if (seq_type == SeqType::SEQ_POMO) {
        for (iterator it = begin(); it != end(); ++it) {
            for (Pattern::iterator it2 = it->begin(); it2 != it->end(); ++it2) {
                abs_state_freq[convertPomoState((int)*it2)] += it->frequency;
            }
        }
    } else {
        for (iterator it = begin(); it != end(); ++it) {
            for (Pattern::iterator it2 = it->begin(); it2 != it->end(); ++it2) {
                if (static_cast<int>((*it2)) < num_states) {
                    abs_state_freq[(int)*it2] += it->frequency;
                }
            }
        }
    }
}


void Alignment::countStatePerSequence (unsigned *count_per_sequence) {
    size_t nseqs = getNSeq();
    memset(count_per_sequence, 0, sizeof(unsigned)*num_states*nseqs);
    for (iterator it = begin(); it != end(); ++it)
        for (size_t i = 0; i != nseqs; ++i) {
            int state = convertPomoState(it->at(i));
            if (state < num_states) {
                count_per_sequence[i*num_states + state] += it->frequency;
            }
        }
}

void Alignment::computeStateFreqPerSequence (double *freq_per_sequence) {
    intptr_t  nseqs          = getNSeq();
    double*   states_app     = new double[num_states*(STATE_UNKNOWN+1)];
    double*   new_freq       = new double[num_states];
    unsigned* state_count    = new unsigned[(STATE_UNKNOWN+1)*nseqs];
    double*   new_state_freq = new double[num_states];
    memset(state_count, 0, sizeof(unsigned)*(STATE_UNKNOWN+1)*nseqs);

    for (int i = 0; i <= static_cast<int>(STATE_UNKNOWN); i++) {
        getAppearance(i, &states_app[i*num_states]);
    }
    for (iterator it = begin(); it != end(); ++it) {
        for (intptr_t i = 0; i != nseqs; ++i) {
            state_count[i*(STATE_UNKNOWN+1) + it->at(i)] += it->frequency;
        }
    }
    double equal_freq = 1.0/num_states;
    for (intptr_t i = 0; i < num_states*nseqs; ++i) {
        freq_per_sequence[i] = equal_freq;
    }
    const int NUM_TIME = 8;
    for (int k = 0; k < NUM_TIME; ++k) {
        for (intptr_t seq = 0; seq < nseqs; ++seq) {
            double *state_freq = &freq_per_sequence[seq*num_states];
            memset(new_state_freq, 0, sizeof(double)*num_states);
            for (int i = 0; i <= static_cast<int>(STATE_UNKNOWN); ++i) {
                if (state_count[seq*(STATE_UNKNOWN+1)+i] == 0) {
                    continue;
                }
                double sum_freq = 0.0;
                for (int j = 0; j < num_states; ++j) {
                    new_freq[j] = state_freq[j] * states_app[i*num_states+j];
                    sum_freq += new_freq[j];
                }
                sum_freq = 1.0/sum_freq;
                for (int j = 0; j < num_states; ++j) {
                    new_state_freq[j] += new_freq[j]*sum_freq*state_count[seq*(STATE_UNKNOWN+1)+i];
                }
            }
            double sum_freq = 0.0;
            for (int j = 0; j < num_states; ++j) {
                sum_freq += new_state_freq[j];
            }
            sum_freq = 1.0/sum_freq;
            for (int j = 0; j < num_states; ++j) {
                state_freq[j] = new_state_freq[j]*sum_freq;
            }
         }
    }

    delete [] new_state_freq;
    delete [] state_count;
    delete [] new_freq;
    delete [] states_app;
}

void Alignment::getAppearance(StateType state, 
                              double *state_app) const {
    int i;
    if (state == STATE_UNKNOWN) {
        for (i = 0; i < num_states; i++) {
            state_app[i] = 1.0;
        }
        return;
    }
    memset(state_app, 0, num_states * sizeof(double));
    if (static_cast<int>(state) < num_states) {
        state_app[(int)state] = 1.0;
        return;
    }
	// ambiguous characters
	int ambi_aa[] = {4+8, 32+64, 512+1024};
	switch (seq_type) {
	case SeqType::SEQ_DNA:
	    state -= (num_states-1);
		for (i = 0; i < num_states; i++)
			if (state & (1 << i)) {
				state_app[i] = 1.0;
			}
		break;
	case SeqType::SEQ_PROTEIN:
		ASSERT(state<23);
		state -= 20;
		for (i = 0; i < 11; i++)
			if (ambi_aa[(int)state] & (1<<i)) {
				state_app[i] = 1.0;
			}
		break;
    case SeqType::SEQ_POMO:
//        state -= num_states;
//        assert(state < pomo_sampled_states.size());
//        // count the number of nucleotides
//        state_app[pomo_sampled_states[state] & 3] = 1.0;
//        state_app[(pomo_sampled_states[state] >> 16) & 3] = 1.0;
        state_app[convertPomoState(state)] = 1.0;
        break;
	default: ASSERT(0); break;
	}
}

void Alignment::getAppearance(StateType state, 
                              StateBitset &state_app) const {
	int i;
    if (state == STATE_UNKNOWN) {
    	state_app.set();
        return;
    }
    state_app.reset();
    if (static_cast<int>(state) < num_states) {
        state_app[(int)state] = 1;
        return;
    }
	// ambiguous characters
	int ambi_aa[] = {4+8, 32+64, 512+1024};
	switch (seq_type) {
	case SeqType::SEQ_DNA:
	    state -= (num_states-1);
		for (i = 0; i < num_states; i++)
			if (state & (1 << i)) {
				state_app[i] = 1;
			}
		break;
	case SeqType::SEQ_PROTEIN:
		if (state >= 23) return;
		state -= 20;
		for (i = 0; i < 11; i++)
			if (ambi_aa[(int)state] & (1<<i)) {
				state_app[i] = 1;
			}
		break;
    case SeqType::SEQ_POMO:
//        state -= num_states;
//        assert(state < pomo_sampled_states.size());
//        // count the number of nucleotides
//        state_app[pomo_sampled_states[state] & 3] = 1;
//        state_app[(pomo_sampled_states[state] >> 16) & 3] = 1;
        state_app[convertPomoState(state)] = 1;
        break;
	default: ASSERT(0); break;
	}
}

UINT Alignment::getCountOfSingletonParsimonyStates() const {
    return total_singleton_parsimony_states;
}

void Alignment::computeCodonFreq_1x4(double *state_freq, 
                                     double *ntfreq) {
	intptr_t nseqs = getNSeq();
    memset(ntfreq, 0, sizeof(double)*4);
    for (iterator it = begin(); it != end(); ++it) {
        for (intptr_t seq = 0; seq < nseqs; ++seq) {
            if ((*it)[seq] == STATE_UNKNOWN) {
                continue;
            }
            int codon = codon_table[(int)(*it)[seq]];
//				int codon = (int)(*it)[seq];
            int nt1 = codon / 16;
            int nt2 = (codon % 16) / 4;
            int nt3 = codon % 4;
            ntfreq[nt1] += (*it).frequency;
            ntfreq[nt2] += (*it).frequency;
            ntfreq[nt3] += (*it).frequency;
        }
    }
    double sum = 0;
    for (int i = 0; i < 4; i++) {
        sum += ntfreq[i];
    }
    for (int i = 0; i < 4; i++) {
        ntfreq[i] /= sum;
    }
    if (verbose_mode >= VerboseMode::VB_MED) {
        for (int i = 0; i < 4; i++) {
            std::cout << "  " << symbols_dna[i] << ": " << ntfreq[i];
        }
        std::cout << endl;
    }
    memcpy(ntfreq+4, ntfreq, sizeof(double)*4);
    memcpy(ntfreq+8, ntfreq, sizeof(double)*4);
    sum = 0.0;
    for (int i = 0; i < num_states; i++) {
        int codon = codon_table[i];
        state_freq[i] = ntfreq[codon/16] * ntfreq[(codon%16)/4] * ntfreq[codon%4];
        if (isStopCodon(i)) {
//                sum_stop += state_freq[i];
            state_freq[i] = Params::getInstance().min_state_freq;
        } else {
            sum += state_freq[i];
        }
    }
//        sum = (1.0-sum)/(1.0-sum_stop);
    sum = 1.0/sum;
    for (int i = 0; i < num_states; i++) {
        if (!isStopCodon(i)) {
            state_freq[i] *= sum;
        }
    }
    sum = 0.0;
    for (int i = 0; i < num_states; i++) {
            sum += state_freq[i];
    }
    ASSERT(fabs(sum-1.0)<1e-5);
}

void Alignment::computeCodonFreq_3x4(double *state_freq, 
                                     double *ntfreq) {
    // F3x4 frequency model
    intptr_t nseqs = getNSeq();
    memset(ntfreq, 0, sizeof(double)*12);
    for (iterator it = begin(); it != end(); ++it) {
        for (intptr_t seq = 0; seq < nseqs; ++seq) {
            if ((*it)[seq] == STATE_UNKNOWN) {
                continue;
            }
            int codon = codon_table[(int)(*it)[seq]];
//				int codon = (int)(*it)[seq];
            int nt1 = codon / 16;
            int nt2 = (codon % 16) / 4;
            int nt3 = codon % 4;
            ntfreq[nt1] += (*it).frequency;
            ntfreq[4+nt2] += (*it).frequency;
            ntfreq[8+nt3] += (*it).frequency;
        }
    }
    for (int j = 0; j < 12; j+=4) {
        double sum = 0;
        for (int i = 0; i < 4; i++) {
            sum += ntfreq[i + j];
        }
        for (int i = 0; i < 4; i++) {
            ntfreq[i + j] /= sum;
        }
        if (verbose_mode >= VerboseMode::VB_MED) {
            for (int i = 0; i < 4; i++) {
                std::cout << "  " << symbols_dna[i] << ": " << ntfreq[i + j];
            }
            std::cout << endl;
        }
    }

    // double sum_stop=0.0;
    double sum = 0.0;
    for (int i = 0; i < num_states; i++) {
        int codon = codon_table[i];
        state_freq[i] = ntfreq[codon/16] * ntfreq[4+(codon%16)/4] * ntfreq[8+codon%4];
        if (isStopCodon(i)) {
            //sum_stop += state_freq[i];
            state_freq[i] = Params::getInstance().min_state_freq;
        } else {
            sum += state_freq[i];
        }
    }

    // sum = (1.0-sum)/(1.0-sum_stop);
    sum = 1.0 / sum;
    for (int i = 0; i < num_states; i++) {
        if (!isStopCodon(i)) {
            state_freq[i] *= sum;
        }
    }
    sum = 0.0;
    for (int i = 0; i < num_states; i++) {
        sum += state_freq[i];
    }
    ASSERT(fabs(sum-1.0)<1e-5);

//		double sum = 0;
//		for (i = 0; i < num_states; i++)
//			if (isStopCodon(i)) {
//				state_freq[i] = 0.0;
//			} else {
//				//int codon = codon_table[i];
//				int codon = i;
//				state_freq[i] = ntfreq[codon/16] * ntfreq[4+(codon%16)/4] * ntfreq[8+codon%4];
//				sum += state_freq[i];
//			}
//		for (i = 0; i < num_states; i++)
//			state_freq[i] /= sum;

        // now recompute ntfreq based on state_freq
//        memset(ntfreq, 0, 12*sizeof(double));
//        for (i = 0; i < num_states; i++)
//            if (!isStopCodon(i)) {
//				int nt1 = i / 16;
//				int nt2 = (i % 16) / 4;
//				int nt3 = i % 4;
//                ntfreq[nt1] += state_freq[i];
//                ntfreq[nt2+4] += state_freq[i];
//                ntfreq[nt3+8] += state_freq[i];
//            }
//		for (j = 0; j < 12; j+=4) {
//			double sum = 0;
//			for (i = 0; i < 4; i++)
//				sum += ntfreq[i+j];
//			for (i = 0; i < 4; i++)
//				ntfreq[i+j] /= sum;
//			if (verbose_mode >= VerboseMode::VB_MED) {
//				for (i = 0; i < 4; i++)
//					cout << "  " << symbols_dna[i] << ": " << ntfreq[i+j];
//				cout << endl;
//			}
//		}
}

void Alignment::computeEmpiricalFrequencies(double *state_freq) {
	intptr_t nseqs = getNSeq();
    memset(state_freq, 0, num_states*sizeof(double));
    int i = 0;
    for (iterator it = begin(); it != end(); ++it, ++i)
        for (intptr_t seq = 0; seq < nseqs; seq++) {
            int state = it->at(seq);
            if (state >= num_states) {
                continue;
            }
            state_freq[state] += it->frequency;
        }
    double sum = 0.0;
    for (i = 0; i < num_states; i++) {
        sum += state_freq[i];
    }
    for (i = 0; i < num_states; i++) {
        state_freq[i] /= sum;
    }
}

void Alignment::computeCodonFreq(StateFreqType freq,  
                                 double *state_freq, double *ntfreq) {
	if (freq == StateFreqType::FREQ_CODON_1x4) {
        computeCodonFreq_1x4(state_freq, ntfreq);
	} else if (freq == StateFreqType::FREQ_CODON_3x4) {
        computeCodonFreq_3x4(state_freq, ntfreq);
	} else if (freq == StateFreqType::FREQ_CODON_3x4C) {
        outError("F3X4C not yet implemented."
                 " Contact authors if you really need it.");
	} else if (freq == StateFreqType::FREQ_EMPIRICAL || 
               freq == StateFreqType::FREQ_ESTIMATE) {
        computeEmpiricalFrequencies(state_freq);
	} else {
        outError("Unsupported codon frequency");
    }
	convfreq(state_freq);
}

void Alignment::computeDivergenceMatrix(double *pair_freq,
                                        double *state_freq,
                                        bool normalize) {
    int i, j;
    ASSERT(pair_freq);
    size_t nseqs = getNSeq();
    memset(pair_freq, 0, sizeof(double)*num_states*num_states);
    memset(state_freq, 0, sizeof(double)*num_states);

    uint64_t *site_state_freq = new uint64_t[STATE_UNKNOWN+1];

    // count pair_freq over all sites
    for (iterator it = begin(); it != end(); ++it) {
        memset(site_state_freq, 0, sizeof(uint64_t)*(STATE_UNKNOWN+1));
        for (i = 0; i < nseqs; i++) {
            site_state_freq[it->at(i)]++;
        }
        for (i = 0; i < num_states; i++) {
            if (site_state_freq[i] == 0) continue;
            state_freq[i] += site_state_freq[i];
            double *pair_freq_ptr = pair_freq + (i*num_states);
            double n = static_cast<double>(site_state_freq[i]);
            pair_freq_ptr[i] += (n*(n-1.0)/2.0)*it->frequency;
            for (j = i + 1; j < num_states; ++j) {
                pair_freq_ptr[j] += site_state_freq[i] * site_state_freq[j] * it->frequency;
            }
        }
    }

    // symmerize pair_freq
    for (i = 0; i < num_states; i++)
        for (j = 0; j < num_states; j++)
            pair_freq[j*num_states+i] = pair_freq[i*num_states+j];

    if (normalize) {
        double sum = 0.0;
        for (i = 0; i < num_states; i++)
            sum += state_freq[i];
        sum = 1.0/sum;
        for (i = 0; i < num_states; i++)
            state_freq[i] *= sum;
        for (i = 0; i < num_states; i++) {
            sum = 0.0;
            double *pair_freq_ptr = pair_freq + (i*num_states);
            for (j = 0; j < num_states; j++)
                sum += pair_freq_ptr[j];
            sum = 1.0/sum;
            for (j = 0; j < num_states; j++)
                pair_freq_ptr[j] *= sum;
        }
    }
    delete [] site_state_freq;
}

double binomial_cdf(int x, int n, double p) {
    ASSERT(p > 0.0 && p < 1.0 && x <= n && x >= 0);
    double cdf = 0.0;
    double b = 0;
    double logp = log(p), log1p = log(1-p);
    for (int k = 0; k < x; k++) {
        if (k > 0) {
            b += log(n-k+1) - log(k);
        }
        double log_pmf_k = b + k * logp + (n-k) * log1p;
        cdf += exp(log_pmf_k);
    }
    if (cdf > 1.0) cdf = 1.0;
    return 1.0-cdf;
}

void SymTestResult::computePvalue() {
    if (significant_pairs <= 0) {
        pvalue_binom = 1.0;
        return;
    }
#ifdef USE_BOOST
    boost::math::binomial binom(included_pairs, Params::getInstance().symtest_pcutoff);
    pvalue_binom = cdf(complement(binom, significant_pairs-1));
#else
    pvalue_binom = binomial_cdf(significant_pairs, included_pairs, Params::getInstance().symtest_pcutoff);
#endif
}

std::ostream& operator<<(std::ostream& stream, const SymTestResult& res) {
    stream << res.significant_pairs << ","
    << res.included_pairs - res.significant_pairs << ",";
    if (Params::getInstance().symtest == SYMTEST_BINOM)
        stream << res.pvalue_binom;
    else
        stream << res.pvalue_maxdiv;
    if (Params::getInstance().symtest_shuffle > 1)
        stream << "," << res.max_stat << ',' << res.pvalue_perm;
    return stream;
}

void Alignment::doSymTest(size_t vecid, vector<SymTestResult> &vec_sym,
                          vector<SymTestResult> &vec_marsym,
                          vector<SymTestResult> &vec_intsym,
                          int *rstream, vector<SymTestStat> *stats)
{
    size_t nseq = getNSeq();

    const double chi2_cutoff = Params::getInstance().symtest_pcutoff;
    
    SymTestResult sym, marsym, intsym;
    sym.max_stat = -1.0;
    marsym.max_stat = -1.0;
    intsym.max_stat = -1.0;
    sym.pvalue_maxdiv = 1.0;
    marsym.pvalue_maxdiv = 1.0;
    intsym.pvalue_maxdiv = 1.0;
    
    vector<Pattern> ptn_shuffled;
    
    if (rstream) {
        // random shuffle alignment columns
        intptr_t nsite = getNSite();
        for (intptr_t site = 0; site < nsite; site++) {
            Pattern ptn = getPattern(site);
            my_random_shuffle(ptn.begin(), ptn.end(), rstream);
            ptn_shuffled.push_back(ptn);
        }
    }
    if (stats)
    {
        stats->reserve(nseq*(nseq-1)/2);
    }
    double max_divergence = 0.0;
    
    for (int seq1 = 0; seq1 < nseq; seq1++) {
        for (int seq2 = seq1+1; seq2 < nseq; seq2++) {
            MatrixXd pair_freq = MatrixXd::Zero(num_states, num_states);
            if (rstream) {
                for (auto it = ptn_shuffled.begin(); it != ptn_shuffled.end(); it++)
                    if (static_cast<int>(it->at(seq1)) < num_states &&
                        static_cast<int>(it->at(seq2)) < num_states)
                        pair_freq(it->at(seq1), it->at(seq2))++;

            } else {
                for (auto it = begin(); it != end(); it++) {
                    if (static_cast<int>(it->at(seq1)) < num_states &&
                        static_cast<int>(it->at(seq2)) < num_states)
                        pair_freq(it->at(seq1), it->at(seq2)) += it->frequency;
                }
            }
            
            // 2020-06-03: Bug fix found by Peter Foster
            double sum_elems  = pair_freq.sum();
            double divergence = (sum_elems == 0.0)
                              ? 0.0 : (sum_elems - pair_freq.diagonal().sum()) / sum_elems;
            
            // performing test of symmetry
            int i, j;
            
            SymTestStat stat;
            stat.seq1 = seq1;
            stat.seq2 = seq2;
            stat.pval_sym = nan("");
            stat.pval_marsym = nan("");
            stat.pval_intsym = nan("");
            
            int df_sym = num_states*(num_states-1)/2;
            bool applicable = true;
            MatrixXd sum = (pair_freq + pair_freq.transpose());
            ArrayXXd res = (pair_freq - pair_freq.transpose()).array().square() / sum.array();

            for (i = 0; i < num_states; i++)
                for (j = i+1; j < num_states; j++) {
                    if (!std::isnan(res(i,j))) {
                        stat.chi2_sym += res(i,j);
                    } else {
                        if (Params::getInstance().symtest_keep_zero)
                            applicable = false;
                        df_sym--;
                    }
                }
            if (df_sym == 0)
                applicable = false;
            
            if (applicable) {
                stat.pval_sym = chi2prob(df_sym, stat.chi2_sym);
                if (stat.pval_sym < chi2_cutoff)
                    sym.significant_pairs++;
                sym.included_pairs++;
                if (sym.max_stat < stat.chi2_sym)
                    sym.max_stat = stat.chi2_sym;
            } else {
                sym.excluded_pairs++;
            }

            // performing test of marginal symmetry
            VectorXd row_sum = pair_freq.rowwise().sum().head(num_states-1);
            VectorXd col_sum = pair_freq.colwise().sum().head(num_states-1);
            VectorXd U = (row_sum - col_sum);
            MatrixXd V = (row_sum + col_sum).asDiagonal();
            V -= sum.topLeftCorner(num_states-1, num_states-1);
                
            FullPivLU<MatrixXd> lu(V);

            if (lu.isInvertible()) {
                stat.chi2_marsym = U.transpose() * lu.inverse() * U;
                int df_marsym = num_states-1;
                stat.pval_marsym = chi2prob(df_marsym, stat.chi2_marsym);
                if (stat.pval_marsym < chi2_cutoff)
                    marsym.significant_pairs++;
                marsym.included_pairs++;
                if (marsym.max_stat < stat.chi2_marsym)
                    marsym.max_stat = stat.chi2_marsym;

                // internal symmetry
                stat.chi2_intsym = stat.chi2_sym - stat.chi2_marsym;
                int df_intsym = df_sym - df_marsym;
                if (df_intsym > 0 && applicable) {
                    stat.pval_intsym = chi2prob(df_intsym, stat.chi2_intsym);
                    if (stat.pval_intsym < chi2_cutoff)
                        intsym.significant_pairs++;
                    intsym.included_pairs++;
                    if (intsym.max_stat < stat.chi2_intsym)
                        intsym.max_stat = stat.chi2_intsym;
                } else
                    intsym.excluded_pairs++;
            } else {
                marsym.excluded_pairs++;
                intsym.excluded_pairs++;
            }
            if (stats)
                stats->push_back(stat);
            if (divergence > max_divergence) {
                sym.pvalue_maxdiv = stat.pval_sym;
                intsym.pvalue_maxdiv = stat.pval_intsym;
                marsym.pvalue_maxdiv = stat.pval_marsym;
                max_divergence = divergence;
            } else if (divergence == max_divergence &&
                       random_double(rstream) < 0.5) {
                sym.pvalue_maxdiv = stat.pval_sym;
                intsym.pvalue_maxdiv = stat.pval_intsym;
                marsym.pvalue_maxdiv = stat.pval_marsym;
            }
        }
    }
    sym.computePvalue();
    marsym.computePvalue();
    intsym.computePvalue();
    vec_sym[vecid] = sym;
    vec_marsym[vecid] = marsym;
    vec_intsym[vecid] = intsym;
}

void Alignment::convfreq(double *stateFrqArr) const {

    if (Params::getInstance().keep_zero_freq) {
        return;
    }
	int i, maxi=0;
	double maxfreq, sum;
	int zero_states = 0;

	sum = 0.0;
	maxfreq = 0.0;
	for (i = 0; i < num_states; ++i) {
		double freq = stateFrqArr[i];
        // Do not check for a minimum frequency with PoMo because very
        // low frequencies are expected for polymorphic sites.
		if ((freq < Params::getInstance().min_state_freq) &&
            (seq_type != SeqType::SEQ_POMO)) {
			stateFrqArr[i] = Params::getInstance().min_state_freq;
		}
		if (freq > maxfreq) {
			maxfreq = freq;
			maxi = i;
		}
		sum += stateFrqArr[i];
	}
	stateFrqArr[maxi] += 1.0 - sum;

	// make state frequencies a bit different from each other
//	for (i = 0; i < num_states - 1; i++)
//		if (!isStopCodon(i))
//			for (j = i + 1; j < num_states; j++)
//				if (!isStopCodon(j))
//					if (stateFrqArr[i] == stateFrqArr[j]) {
//						stateFrqArr[i] += MIN_FREQUENCY_DIFF;
//						stateFrqArr[j] -= MIN_FREQUENCY_DIFF;
//					}
	if (zero_states) {
		cout << "WARNING: " << zero_states << " states not present"
             << " in alignment that might cause numerical instability" << endl;
	}
} /* convfreq */

double Alignment::computeUnconstrainedLogL() const {
    intptr_t nptn = size();
    double logl = 0.0;
    int nsite = getNSite32();
    double lognsite = log(nsite);
    for (intptr_t i = 0; i < nptn; i++) {
        logl += (log(at(i).frequency) - lognsite) * at(i).frequency;
    }
    return logl;
}

void Alignment::printSiteGaps(const char *filename) {
    try {
        ofstream out;
        out.exceptions(ios::failbit | ios::badbit);

        out.open(filename);
        int nsite = getNSite32();
        out << nsite << endl << "Site_Gap  ";
        for (int site = 0; site < nsite; ++site) {
            out << " " << at(getPatternID(site)).computeGapChar(num_states, STATE_UNKNOWN);
        }
        out << endl << "Site_Ambi ";
        for (size_t site = 0; site < getNSite(); ++site) {
            out << " " << at(getPatternID(site)).computeAmbiguousChar(num_states);
        }
        out << endl;
        out.close();
        cout << "Site gap-counts printed to " << filename << endl;
    } catch (ios::failure) {
        outError(ERR_WRITE_OUTPUT, filename);
    }
}

void Alignment::getPatternFreq(IntVector &freq) {
    freq.resize(getNPattern());
    int cnt = 0;
    for (iterator it = begin(); it < end(); ++it, ++cnt) {
        freq[cnt] = (*it).frequency;
    }
}

void Alignment::getPatternFreq(int *freq) {
    int cnt = 0;
    for (iterator it = begin(); it < end(); ++it, ++cnt) {
        freq[cnt] = (*it).frequency;
    }
}

//added by MA
void Alignment::multinomialProb(Alignment refAlign, double &prob) {
    // 	cout << "Computing the probability of this alignment"
    //          " given the multinomial distribution"
    //          " determined by a reference alignment ..." << endl;
    //should we check for compatibility of sequence's names
    //and sequence's order in THIS alignment and in the objectAlign??
    //check alignment length
    int nsite = getNSite32();
    ASSERT(nsite == refAlign.getNSite());
    double sumFac  = 0;
    double sumProb = 0;
    double fac     = logFac(nsite);
    for ( iterator it = begin(); it != end() ; it++) {
        PatternIntMap::iterator pat_it = refAlign.pattern_index.find((*it));
        if ( pat_it == refAlign.pattern_index.end() ) { //not found ==> error
            outError("Pattern in the current alignment"
                     " is not found in the reference alignment!");
    }
        sumFac += logFac((*it).frequency);
        int index = pat_it->second;
        sumProb += (double)(*it).frequency*log((double)refAlign.at(index).frequency/(double)nsite);
    }
    prob = fac - sumFac + sumProb;
}

void Alignment::multinomialProb (DoubleVector logLL, double &prob)
{
    //cout << "Function in Alignment: Compute" 
    //     << " probability of the expected alignment"
    //     << " (determined by patterns log-likelihood" 
    //     << " under some tree and model) given THIS alignment." << endl;

    //The expected normalized requencies
    IntVector expectedNorFre;

    if (logLL.empty()) {
        outError("Error: log likelihood of patterns are not given!");
    }
    intptr_t patNum = getNPattern();

    ASSERT(logLL.size() == patNum);

    intptr_t alignLen = getNSite();
    //resize the expectedNorFre vector
    expectedNorFre.resize(patNum,-1);

    //Vector containing the 'relative' likelihood of the pattern p_i
    DoubleVector LL(patNum,-1.0);
    double sumLL = 0; //sum of the likelihood of the patterns in the alignment
    double max_logl = *max_element(logLL.begin(), logLL.end()); // to rescale the log-likelihood
    //Compute the `relative' (to the first pattern) likelihood from the logLL
    for ( int i = 0; i < patNum; i++ )
    {
        LL[i] = exp(logLL[i]-max_logl);
        //LL[i] = exp(logLL[i]);
        sumLL += LL[i];
    }

    //Vector containing l_i = p_i*ell/sum_i(p_i)
    DoubleVector ell(patNum, -1.0);
    //Compute l_i
    for ( int i = 0; i < patNum; i++ )
    {
        ell[i] = (double)alignLen * LL[i] / sumLL;
    }

    //Vector containing r_i where r_0 = ell_0; r_{i+1} = ell_{i+1} + r_i - ordinaryRounding(r_i)
    DoubleVector r(patNum, -1.0);
    //Compute r_i and the expected normalized frequencies
    r[0] = ell[0];
    expectedNorFre[0] = (int)floor(ell[0]+0.5); //note that floor(_number+0.5) returns the ordinary rounding of _number
    //int sum = expectedNorFre[0];
    for (int j = 1; j < patNum; j++ )
    {
        r[j] = ell[j] + r[j-1] - floor(r[j-1]+0.5);
        expectedNorFre[j] = (int)floor(r[j]+0.5);
        //sum += expectedNorFre[j];
    }

    //cout << "Number of patterns: " << patNum
    //     << ", sum of expected sites: " << sum << endl;
    //return expectedNorFre;
    //compute the probability of having expectedNorFre,
    //given the observed pattern frequencies of THIS alignment
    double sumFac = 0;
    double sumProb = 0;
    double fac = logFac(static_cast<int>(alignLen));
    for (int patID = 0; patID < patNum; patID++) {
        int patFre = expectedNorFre[patID];
        sumFac  += logFac(patFre);
        sumProb += (double)patFre*log((double)at(patID).frequency/(double)alignLen);
    }
    prob = fac - sumFac + sumProb;
}

void Alignment::multinomialProb (double *logLL, double &prob)
{
    IntVector expectedNorFre; //The expected normalized requencies

    intptr_t patNum   = getNPattern();
    intptr_t alignLen = getNSite();

    //resize the expectedNorFre vector
    expectedNorFre.resize(patNum,-1);

    //Vector containing the 'relative' likelihood of the pattern p_i
    DoubleVector LL(patNum,-1.0);
    double sumLL = 0; //sum of the likelihood of the patterns in the alignment
    double max_logl = *max_element(logLL, logLL + patNum); // to rescale the log-likelihood
    //Compute the `relative' (to the first pattern) likelihood from the logLL
    for ( int i = 0; i < patNum; i++ ) {
        LL[i] = exp(logLL[i]-max_logl);
        //LL[i] = exp(logLL[i]);
        sumLL += LL[i];
    }

    //Vector containing l_i = p_i*ell/sum_i(p_i)
    DoubleVector ell(patNum, -1.0);
    //Compute l_i
    for ( int i = 0; i < patNum; i++ ) {
        ell[i] = (double)alignLen * LL[i] / sumLL;
    }

    //Vector containing r_i where r_0 = ell_0; r_{i+1} = ell_{i+1} + r_i - ordinaryRounding(r_i)
    DoubleVector r(patNum, -1.0);
    //Compute r_i and the expected normalized frequencies
    r[0] = ell[0];
    expectedNorFre[0] = (int)floor(ell[0]+0.5); //note that floor(_number+0.5) returns the ordinary rounding of _number
    //int sum = expectedNorFre[0];
    for (int j = 1; j < patNum; j++ ) {
        r[j] = ell[j] + r[j-1] - floor(r[j-1]+0.5);
        expectedNorFre[j] = (int)floor(r[j]+0.5);
        //sum += expectedNorFre[j];
    }

    //cout << "Number of patterns: " << patNum
    //     << ", sum of expected sites: " << sum << endl;
    //return expectedNorFre;
    //compute the probability of having expectedNorFre,
    //given the observed pattern frequencies of THIS alignment
    double sumFac = 0;
    double sumProb = 0;
    double fac = logFac(static_cast<int>(alignLen));
    for (int patID = 0; patID < patNum; patID++) {
        int patFre = expectedNorFre[patID];
        sumFac += logFac(patFre);
        sumProb += (double)patFre*log((double)at(patID).frequency/(double)alignLen);
    }
    prob = fac - sumFac + sumProb;
}

double Alignment::multinomialProb (IntVector &pattern_freq)
{
    //cout << "Function in Alignment: Compute probability"
    //     << " of the expected alignment"
    //     << " (determined by patterns log-likelihood"
    //     << " under some tree and model)"
    //     << " given THIS alignment." << endl;

    //The expected normalized requencies

    //cout << "Number of patterns: " << patNum
    //     << ", sum of expected sites: " << sum << endl;
    //return expectedNorFre;
    //compute the probability of having expectedNorFre,
    //given the observed pattern frequencies of THIS alignment
    ASSERT(size() == pattern_freq.size());
    intptr_t patNum   = getNPattern();
    intptr_t alignLen = getNSite();
    double sumFac = 0;
    double sumProb = 0;
    double fac = logFac(static_cast<int>(alignLen));
    for (intptr_t patID = 0; patID < patNum; patID++) {
        int patFre = pattern_freq[patID];
        sumFac += logFac(patFre);
        sumProb += (double)patFre*log((double)at(patID).frequency/(double)alignLen);
    }
    return (fac - sumFac + sumProb);
}

bool Alignment::readSiteStateFreq(const char* site_freq_file)
{
    cout << endl << "Reading site-specific state frequency file "
        << site_freq_file << " ..." << endl;
    site_model.resize(getNSite(), -1);
    IntVector pattern_to_site; // vector from pattern to the first site
    pattern_to_site.resize(getNPattern(), -1);
    for (int i = 0; i < static_cast<int>(getNSite()); ++i) {
        if (pattern_to_site[getPatternID(i)] == -1) {
            pattern_to_site[getPatternID(i)] = i;
        }
    }
    bool aln_changed = false;

	try {
		ifstream in;
		in.exceptions(ios::failbit | ios::badbit);
		in.open(site_freq_file);
		in.exceptions(ios::badbit);

        readSiteStateFreqFromFile(in, pattern_to_site, 
                                  aln_changed);

        in.clear();
        // set the failbit again
        in.exceptions(ios::failbit | ios::badbit);
        in.close();
    } catch (const char* str) {
        outError(str);
    } catch (string& str) {
        outError(str);
    } catch(ios::failure) {
        outError(ERR_READ_INPUT);
    }
    if (aln_changed) {
        cout << "Regrouping alignment sites..." << endl;
        regroupSitePattern(static_cast<int>(site_state_freq.size()), site_model);
    }
    cout << site_state_freq.size()
         << " distinct per-site state frequency vectors detected" << endl;
    return aln_changed;
}

void Alignment::readSiteStateFreqFromFile
        (ifstream& in, const IntVector& pattern_to_site,
         bool& aln_changed) {
    double freq;
    string site_spec;
    int    specified_sites = 0;

    for (int model_id = 0; !in.eof(); model_id++) {
        // remove the failbit
        in >> site_spec;
        if (in.eof()) {
            break;
        }
        IntVector site_id;
        extractSiteID(this, site_spec.c_str(), site_id);
        specified_sites += static_cast<int>(site_id.size());
        if (site_id.size() == 0) {
            throw "No site ID specified";
        }
        for (auto it = site_id.begin(); it != site_id.end(); it++) {
            if (site_model[*it] != -1) {
                throw "Duplicated site ID";
            }
            site_model[*it] = static_cast<int>(site_state_freq.size());
        }
        double *site_freq_entry = new double[num_states];
        double sum = 0;
        for (int i = 0; i < num_states; ++i) {
            in >> freq;
            if (freq <= 0.0 || freq >= 1.0) {
                throw "Frequencies must be strictly positive and smaller than 1";
            }
            site_freq_entry[i] = freq;
            sum += freq;
        }
        if (fabs(sum-1.0) > 1e-4) {
            if (fabs(sum-1.0) > 1e-3) {
                outWarning("Frequencies of site " + site_spec + 
                    " do not sum up to 1 and will be normalized");
            }
            sum = 1.0/sum;
            for (int i = 0; i < num_states; ++i) {
                site_freq_entry[i] *= sum;
            }
        }
        convfreq(site_freq_entry); // regularize frequencies 
                                   //(eg if some freq = 0)
        checkForEqualityOfSites(pattern_to_site, site_id, 
                                site_freq_entry, aln_changed);
    }
    handleUnspecifiedSites(specified_sites, aln_changed);
}

void Alignment::checkForEqualityOfSites
        (const IntVector& pattern_to_site, const IntVector& site_id,
         double* site_freq_entry, bool& aln_changed) {
    // 2016-02-01: now check for equality of sites
    // with same site-pattern and same freq
    int prev_site = pattern_to_site[getPatternID(site_id[0])];
    if (site_id.size() == 1 && prev_site < site_id[0] &&
        site_model[prev_site] != -1) {
        // compare freq with prev_site
        bool matched_freq = true;
        double *prev_freq = site_state_freq[site_model[prev_site]];
        for (int i = 0; i < num_states; ++i) {
            if (site_freq_entry[i] != prev_freq[i]) {
                matched_freq = false;
                break;
            }
        }
        if (matched_freq) {
            site_model[site_id[0]] = site_model[prev_site];
        } else {
            aln_changed = true;
        }
    }
    if (site_model[site_id[0]] == site_state_freq.size()) {
        site_state_freq.push_back(site_freq_entry);
    }
    else {
        delete [] site_freq_entry;
    }
}

void Alignment::handleUnspecifiedSites
        (int specified_sites, bool& aln_changed) {
    if (specified_sites < site_model.size()) {
        aln_changed = true;
        // there are some unspecified sites
        cout << site_model.size() - specified_sites
             << " unspecified sites will get default frequencies" << endl;
        for (size_t i = 0; i < site_model.size(); ++i) {
            if (site_model[i] == -1) {
                site_model[i] = static_cast<int>(site_state_freq.size());
            }
        }
        site_state_freq.push_back(NULL);
    }
}

void Alignment::showNoProgress() {
    isShowingProgressDisabled = true;
}
