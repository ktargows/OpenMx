/*
  Copyright 2012-2014 Joshua Nathaniel Pritikin and contributors

  This is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef _BA81QUAD_H_
#define _BA81QUAD_H_

#include "glue.h"
#include "Eigen/Core"
#include "libifa-rpf.h"

class ba81NormalQuad {
 private:
	void pointToWhere(const int *quad, double *where, int upto);
	void decodeLocation(int qx, const int dims, int *quad);
	double One, ReciprocalOfOne;

	int sIndex(int sx, int qx) {
		//if (sx < 0 || sx >= state->numSpecific) Rf_error("Out of domain");
		//if (qx < 0 || qx >= state->quadGridSize) Rf_error("Out of domain");
		return qx * numSpecific + sx;
	};

	void mapDenseSpace(double piece, const double *where,
			   const double *whereGram, double *latentDist);
	void mapSpecificSpace(int sgroup, double piece, const double *where,
			      const double *whereGram, double *latentDist);

 public:
	int quadGridSize;                     // rename to gridSize TODO
	int maxDims;
	int primaryDims;
	int numSpecific;
	int maxAbilities;
	std::vector<double> Qpoint;           // quadGridSize
	int totalQuadPoints;                  // quadGridSize ^ maxDims
	int totalPrimaryPoints;               // totalQuadPoints except for specific dim
	int weightTableSize;                  // dense: totalQuadPoints; 2tier: totalQuadPoints * numSpecific
	std::vector<double> priQarea;         // totalPrimaryPoints
	std::vector<double> speQarea;         // quadGridSize * numSpecific
	std::vector<double> wherePrep;        // totalQuadPoints * maxDims
	Eigen::MatrixXd whereGram;            // triangleLoc1(maxDims) x totalQuadPoints

	ba81NormalQuad();
	void setOne(double one) { One = one; ReciprocalOfOne = 1/one; }
	void setup0();
	void setup(double Qwidth, int Qpoints, double *means,
		   Eigen::MatrixXd &priCov, Eigen::VectorXd &sVar);
	double getReciprocalOfOne() const { return ReciprocalOfOne; };

	// For dense cov, Dweight is size totalQuadPoints
	// For two-tier, Dweight is numSpecific x totalQuadPoints
	void EAP(double *thrDweight, double scalingFactor, double *scorePad);
};

class ifaGroup {
 private:
	SEXP Rdata;
	void verifyFactorNames(SEXP mat, const char *matName);
 public:
	const int numThreads;

	// item description related
	std::vector<const double*> spec;
	int itemMaxDims;
	int numItems() const { return (int) spec.size(); }
	int paramRows;
	double *param;  // itemParam->data
	std::vector<const char*> itemNames;
	std::vector<int> itemOutcomes;
	std::vector<int> cumItemOutcomes;
	int totalOutcomes;
	std::vector<int> Sgroup;       // item's specific group 0..numSpecific-1

	// latent distribution
	double qwidth;
	int qpoints;
	ba81NormalQuad quad;
	ba81NormalQuad &getQuad() { return quad; };
	bool twotier;  // rename to detectTwoTier TODO
	int maxAbilities;
	int numSpecific;
	double *mean;
	double *cov;
	std::vector<const char*> factorNames;

	// data related
	SEXP dataRowNames;
	std::vector<const int*> dataColumns;
	std::vector<int> rowMap;       // row index into MxData
	int getNumUnique() const { return (int) rowMap.size(); }
	const char *weightColumnName;
	double *rowWeight;
 private:
	int minItemsPerScore;
 public:
	void setMinItemsPerScore(int mips);
	std::vector<bool> rowSkip;     // whether to treat the row as NA

	// workspace
	double *outcomeProb;                  // totalOutcomes * totalQuadPoints
	static const double SmallestPatternLik;
	int excludedPatterns;
	Eigen::ArrayXd patternLik;            // numUnique

	static bool validPatternLik(double pl)
	{ return std::isfinite(pl) && pl > SmallestPatternLik; }

	// TODO:
	// scores

	ifaGroup(int cores, bool _twotier);
	~ifaGroup();
	void setGridFineness(double width, int points);
	void import(SEXP Rlist);
	void importSpec(SEXP slotValue);
	void setLatentDistribution(int dims, double *mean, double *cov);
	double *getItemParam(int ix) { return param + paramRows * ix; }
	const int *dataColumn(int col) { return dataColumns[col]; };
	void detectTwoTier();
	template <typename T> void buildRowSkip(T, void (*naActionType)(T, int row, int ability));
	void sanityCheck();
	void ba81OutcomeProb(double *param, bool wantLog);
	void ba81LikelihoodSlow2(const int px, double *out);
	void cai2010EiEis(const int px, double *lxk, double *Eis, double *Ei);
	void cai2010part2(double *Qweight, double *Eis, double *Ei);
};

template <typename T>
void ifaGroup::buildRowSkip(T userdata, void (*naActionType)(T, int row, int ability))
{
	rowSkip.assign(rowMap.size(), false);

	if (maxAbilities == 0) return;

	// Rows with no information about an ability will obtain the
	// prior distribution as an ability estimate. This will
	// throw off multigroup latent distribution estimates.
	for (size_t rx=0; rx < rowMap.size(); rx++) {
		std::vector<int> contribution(maxAbilities);
		for (int ix=0; ix < numItems(); ix++) {
			int pick = dataColumn(ix)[ rowMap[rx] ];
			if (pick == NA_INTEGER) continue;
			const double *ispec = spec[ix];
			int dims = ispec[RPF_ISpecDims];
			double *iparam = getItemParam(ix);
			for (int dx=0; dx < dims; dx++) {
				// assume factor loadings are the first item parameters
				if (iparam[dx] == 0) continue;
				contribution[dx] += 1;
			}
		}
		for (int ax=0; ax < maxAbilities; ++ax) {
			if (contribution[ax] < minItemsPerScore) {
				naAction(userdata, rx, ax);

				// We could compute the other scores, but estimation of the
				// latent distribution is in the hot code path. We can reconsider
				// this choice when we try generating scores instead of the
				// score distribution.
				rowSkip[rx] = true;
			}
		}
	}
}

struct BA81Dense {};
struct BA81TwoTier {};

struct BA81EngineBase {
	int getPrimaryPoints(class ifaGroup *state) { return state->quad.totalPrimaryPoints; };
	double getPatLik(class ifaGroup *state, int px, double *lxk);
};

template <typename T, typename CovType>
struct BA81OmitEstep {
	void begin(class ifaGroup *state, T extraData) {};
	void addRow(class ifaGroup *state, T extraData, int px, double *Qweight, int thrId) {};
	void recordTable(class ifaGroup *state, T extraData) {};
	bool hasEnd() { return false; }
};

template <
  typename T,
  typename CovTypePar,
  template <typename> class LatentPolicy,
  template <typename, typename> class EstepPolicy
>
struct BA81Engine : LatentPolicy<T>, EstepPolicy<T, CovTypePar>, BA81EngineBase {
	void ba81Estep1(class ifaGroup *state, T extraData);
};


template <
  typename T,
  template <typename> class LatentPolicy,
  template <typename, typename> class EstepPolicy
>
struct BA81Engine<T, BA81Dense, LatentPolicy, EstepPolicy> :
	LatentPolicy<T>, EstepPolicy<T, BA81Dense>, BA81EngineBase {
	typedef BA81Dense CovType;
	void ba81Estep1(class ifaGroup *state, T extraData);
};

template <
  typename T,
  template <typename> class LatentPolicy,
  template <typename, typename> class EstepPolicy
>
void BA81Engine<T, BA81Dense, LatentPolicy, EstepPolicy>::ba81Estep1(class ifaGroup *state, T extraData)
{
	ba81NormalQuad &quad = state->getQuad();
	const int numUnique = state->getNumUnique();
	const int numThreads = state->numThreads;
	Eigen::VectorXd thrQweight;
	thrQweight.resize(quad.weightTableSize * numThreads);
	state->excludedPatterns = 0;
	state->patternLik.resize(numUnique);
	Eigen::ArrayXd &patternLik = state->patternLik;
	std::vector<bool> &rowSkip = state->rowSkip;

	EstepPolicy<T, CovType>::begin(state, extraData);
	LatentPolicy<T>::begin(state, extraData);

#pragma omp parallel for num_threads(numThreads)
	for (int px=0; px < numUnique; px++) {
		if (rowSkip[px]) {
			patternLik[px] = 0;
			continue;
		}

		int thrId = omp_get_thread_num();
		double *Qweight = thrQweight.data() + quad.weightTableSize * thrId;
		state->ba81LikelihoodSlow2(px, Qweight);

		double patternLik1 = getPatLik(state, px, Qweight);
		if (patternLik1 == 0) continue;

		LatentPolicy<T>::normalizeWeights(state, extraData, px, Qweight, patternLik1, thrId);
		EstepPolicy<T, CovType>::addRow(state, extraData, px, Qweight, thrId);
	}

	if (EstepPolicy<T, CovType>::hasEnd() && LatentPolicy<T>::hasEnd()) {
#pragma omp parallel sections
		{
			{ EstepPolicy<T, CovType>::recordTable(state, extraData); }
#pragma omp section
			{ LatentPolicy<T>::end(state, extraData); }
		}
	} else {
		EstepPolicy<T, CovType>::recordTable(state, extraData);
		LatentPolicy<T>::end(state, extraData);
	}
}

template <
  typename T,
  template <typename> class LatentPolicy,
  template <typename, typename> class EstepPolicy
>
struct BA81Engine<T, BA81TwoTier, LatentPolicy, EstepPolicy> :
	LatentPolicy<T>, EstepPolicy<T, BA81TwoTier>, BA81EngineBase {
	typedef BA81TwoTier CovType;
	void ba81Estep1(class ifaGroup *state, T extraData);
};

template <
  typename T,
  template <typename> class LatentPolicy,
  template <typename, typename> class EstepPolicy
>
void BA81Engine<T, BA81TwoTier, LatentPolicy, EstepPolicy>::ba81Estep1(class ifaGroup *state, T extraData)
{
	ba81NormalQuad &quad = state->getQuad();
	const int numSpecific = quad.numSpecific;
	const int numUnique = state->getNumUnique();
	const int numThreads = state->numThreads;
	Eigen::VectorXd thrQweight;
	thrQweight.resize(quad.weightTableSize * numThreads);
	state->excludedPatterns = 0;
	state->patternLik.resize(numUnique);
	Eigen::ArrayXd &patternLik = state->patternLik;
	std::vector<bool> &rowSkip = state->rowSkip;

	EstepPolicy<T, CovType>::begin(state, extraData);
	LatentPolicy<T>::begin(state, extraData);

	const int totalPrimaryPoints = quad.totalPrimaryPoints;
	Eigen::ArrayXXd thrEi(totalPrimaryPoints, numThreads);
	Eigen::ArrayXXd thrEis(totalPrimaryPoints * numSpecific, numThreads);

#pragma omp parallel for num_threads(numThreads)
	for (int px=0; px < numUnique; px++) {
		if (rowSkip[px]) {
			patternLik[px] = 0;
			continue;
		}

		int thrId = omp_get_thread_num();
		double *Qweight = thrQweight.data() + quad.weightTableSize * thrId;
		double *Ei = &thrEi.coeffRef(0, thrId);
		double *Eis = &thrEis.coeffRef(0, thrId);
		state->cai2010EiEis(px, Qweight, Eis, Ei);

		double patternLik1 = getPatLik(state, px, Ei);
		if (patternLik1 == 0) continue;

		if (!EstepPolicy<T, CovType>::hasEnd() && !LatentPolicy<T>::hasEnd()) continue;

		state->cai2010part2(Qweight, Eis, Ei);

		LatentPolicy<T>::normalizeWeights(state, extraData, px, Qweight, patternLik1, thrId);
		EstepPolicy<T, CovType>::addRow(state, extraData, px, Qweight, thrId);
	}

	if (EstepPolicy<T, CovType>::hasEnd() && LatentPolicy<T>::hasEnd()) {
#pragma omp parallel sections
		{
			{ EstepPolicy<T, CovType>::recordTable(state, extraData); }
#pragma omp section
			{ LatentPolicy<T>::end(state, extraData); }
		}
	} else {
		EstepPolicy<T, CovType>::recordTable(state, extraData);
		LatentPolicy<T>::end(state, extraData);
	}
}

#endif
