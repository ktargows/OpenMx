#ifndef _RAMINTERNAL_H_
#define _RAMINTERNAL_H_

#include <Eigen/Core>
#include <Eigen/SparseCore>
#include <Eigen/SparseLU>
#include <Eigen/CholmodSupport>
#include <Rcpp.h>
#include <RcppEigenStubs.h>
#include <RcppEigenWrap.h>
//#include <Eigen/UmfPackSupport>
#include <RcppEigenCholmod.h>

namespace RelationalRAMExpectation {
	struct addr {
		omxExpectation *model;
		int row;     // to load definition variables (never the key)
		int key;
		int numKids;
		int numJoins;
		int parent1;  // first parent
		int fk1;      // first foreign key

		// clump names indexes into the layout for models that
		// are considered a compound component of this model.
		std::vector<int> clump;

		// split here, below move to placement object TODO
		int modelStart;  //both latent and obs
		int numVars() const;
		int obsStart;
		int numObsCache;
		int numObs() const { return numObsCache; }
		double rampartScale;

		std::string modelName() const {
			std::string tmp = model->data->name;
			tmp = tmp.substr(0, tmp.size() - 5); // remove ".data" suffix
			return tmp;
		};
		static bool CompareWithModelStart(addr &i, int p1) { return i.modelStart < p1; };
	};

	class Amatrix {
	private:
		class state &st;
		bool analyzed;
		int AshallowDepth;
		double signA;
		Eigen::SparseMatrix<double>      ident;
	public:
		// could store coeff extraction plan in addr TODO
		Eigen::SparseMatrix<double>      in;
		Eigen::SparseLU< Eigen::SparseMatrix<double>,
				 Eigen::COLAMDOrdering<Eigen::SparseMatrix<double>::Index> > solver;
		Eigen::SparseMatrix<double>      out;
		//Eigen::UmfPackLU< Eigen::SparseMatrix<double> > Asolver;

		Amatrix(class state &st) : st(st), analyzed(false), AshallowDepth(-1), signA(-1) {};
		void resize(int dim);
		void determineShallowDepth(FitContext *fc);
		void refreshUnitA(FitContext *fc, addr &a1);
		int verbose() const;
		void invertAndFilterA();
		Eigen::SparseMatrix<double> getInputMatrix() const;
	};

	struct RowToOffsetMapCompare {
		bool operator() (const std::pair<omxData*,int> &lhs, const std::pair<omxData*,int> &rhs) const
		{
			if (lhs.first != rhs.first)
				return strcmp(lhs.first->name, rhs.first->name) < 0;
			return lhs.second < rhs.second;
		}
	};

	struct placement {
		int aIndex;                // index into addr vector
		int modelStart;  //both latent and obs
		int obsStart;
	};

	struct independentGroup {
		int numRows;
		std::vector<placement> placements;
		omxMatrix *smallCol;
		double signA;
		Eigen::SparseMatrix<double>      fullS;
		std::vector< std::vector<int> >  rotationPlan;
		std::vector<bool> latentFilter; // use to reduce the A matrix
		SEXP obsNameVec;
		SEXP varNameVec;
		Amatrix regularA;
		Amatrix rampartA;
		Eigen::VectorXd dataVec;
		Eigen::VectorXd fullMeans;
		Eigen::SparseMatrix<double>      fullCov;
	};

	class state {
	private:
		omxMatrix *smallCol;
		Eigen::SparseMatrix<double>      fullS;
		std::vector<int>                 rampartUsage;
		std::vector< std::vector<int> >  rotationPlan;

	public:
		struct omxExpectation *homeEx;
		typedef std::map< std::pair<omxData*,int>, int, RowToOffsetMapCompare> RowToOffsetMapType;
		RowToOffsetMapType               rowToOffsetMap;
		std::vector<addr>		 layout;
		// below move to independentGroup TODO
		std::vector<placement>           placements;
		std::vector<bool>                latentFilter; // use to reduce the A matrix
		SEXP                             obsNameVec;
		SEXP                             varNameVec;
		Amatrix                          regularA;
		Eigen::ArrayXi                   dataColumn; // for OLS profiled constant parameters
		Eigen::VectorXd                  dataVec;
		Eigen::VectorXd                  fullMean;
		Eigen::VectorXd                  expectedMean;
		Eigen::SparseMatrix<double>      fullCov;

	private:
		void refreshModel(FitContext *fc);
		int placeOneRow(omxExpectation *expectation, int frow, int &totalObserved, int &maxSize);
		void examineModel();
		int rampartRotate(int level);
		template <typename T> void oertzenRotate(std::vector<T> &t1);
	public:
		state() : regularA(*this) {};
		~state();
		void computeCov(FitContext *fc);
		void computeMean(FitContext *fc);
		void init(omxExpectation *expectation, FitContext *fc);
		void exportInternalState(MxRList &dbg);
		int verbose() const;
		template <typename T> void applyRotationPlan(Eigen::MatrixBase<T> &resid) const;
		bool hasRotationPlan() const { return rotationPlan.size() != 0; }
	};

	template <typename T>
	void state::applyRotationPlan(Eigen::MatrixBase<T> &resid) const
	{
		//std::string buf;
		for (size_t rx=0; rx < rotationPlan.size(); ++rx) {
			//buf += "rotate";
			const std::vector<int> &om = rotationPlan[rx];
			double partialSum = 0.0;
			for (size_t ox=0; ox < om.size(); ++ox) {
				partialSum += resid[om[ox]];
				//buf += string_snprintf(" %d", 1+ om[ox]);
			}
			double prev = resid[om[0]];
			resid[om[0]] = partialSum / sqrt(om.size());

			for (size_t i=1; i < om.size(); i++) {
				double k=om.size()-i;
				partialSum -= prev;
				double prevContrib = sqrt(k / (k+1)) * prev;
				prev = resid[om[i]];
				resid[om[i]] = partialSum * sqrt(1.0 / (k*(k+1))) - prevContrib;
			}
			//buf += "\n";
		}
		//if (buf.size()) mxLogBig(buf);
	}
};

class omxRAMExpectation {
	bool trivialF;
	int Zversion;
	omxMatrix *_Z;
 public:

 omxRAMExpectation(omxMatrix *Z) : trivialF(false), Zversion(0), _Z(Z) {};
	~omxRAMExpectation() {
		omxFreeMatrix(_Z);
	};

	omxMatrix *getZ(FitContext *fc);
	void CalculateRAMCovarianceAndMeans();

	omxMatrix *cov, *means; // observed covariance and means
	omxMatrix *A, *S, *F, *M, *I;
	omxMatrix *X, *Y, *Ax;

	int verbose;
	int numIters;
	int rampart;
	bool rampartEnabled() { return rampart == NA_INTEGER || rampart > 0; };
	double logDetObserved;
	double n;
	double *work;
	int lwork;

	std::vector< omxMatrix* > between;
	RelationalRAMExpectation::state *rram;

	void ensureTrivialF();
};

namespace RelationalRAMExpectation {
	inline int state::verbose() const
	{
		return ((omxRAMExpectation*) homeEx->argStruct)->verbose;
	}

	inline int Amatrix::verbose() const { return st.verbose(); };
};

#endif
