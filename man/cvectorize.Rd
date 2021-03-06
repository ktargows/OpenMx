%
%   Copyright 2007-2017 The OpenMx Project
%
%   Licensed under the Apache License, Version 2.0 (the "License");
%   you may not use this file except in compliance with the License.
%   You may obtain a copy of the License at
% 
%        http://www.apache.org/licenses/LICENSE-2.0
% 
%   Unless required by applicable law or agreed to in writing, software
%   distributed under the License is distributed on an "AS IS" BASIS,
%   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
%   See the License for the specific language governing permissions and
%   limitations under the License.

\name{cvectorize}
\alias{cvectorize}

\title{Vectorize By Column}

\description{
   This function returns the vectorization of an input matrix in a column by column traversal of the matrix.  The output is returned as a column vector.
}

\usage{
cvectorize(x)
}

\arguments{
   \item{x}{an input matrix.}
}

\seealso{
\code{\link{rvectorize}}, \code{\link{vech}}, \code{\link{vechs}} 
}

\examples{

cvectorize(matrix(1:9, 3, 3))
cvectorize(matrix(1:12, 3, 4))


}
