#include "como_engine.h"

como_engine::como_engine(){ }

como_engine::~como_engine(){ }

bool como_engine::allocate()
{
    mDetector = cv::xfeatures2d::SURF::create(mMinHessian);
	mFuzzificator = new fuzzificator();
	mQuantifier = new quantifier();
	return true;
}

void como_engine::deallocate()
{
	delete mFuzzificator;
	delete mQuantifier;
}

bool como_engine::detect(cv::Mat &image_in, std::vector<cv::KeyPoint> &featurePnts)
{
	if(image_in.empty())
	{
		return false;
	}

    std::vector<cv::KeyPoint> keypoints;
    mDetector->detect(image_in, keypoints);
    
	//std::vector<cv::Point2f> points;

    //TODO: remove debug
    #if defined DEBUG
        cv::Mat debuggingImage;
        cv::drawKeypoints(image_in, keypoints, debuggingImage, cv::Scalar::all(-1), 4);

        //cv::imwrite("/Users/skletz/Dropbox/Programming/CUPCakes/opencv-como/debugging/test.jpg", debuggingImage);
        cv::imwrite("/media/bns/DATA/projects/opencv-como/debugging/al_test.jpg", debuggingImage);
    #endif

	featurePnts.assign(keypoints.begin(), keypoints.end());
	return true;
}

bool como_engine::describe(cv::Mat &image_in, std::vector<cv::KeyPoint> &featurePnts, cv::Mat &descriptors)
{

    bool status = false;

    //TODO: remove
    #if defined DEBUG
        cv::Mat blockImage = image_in.clone();
    #endif

    // loop through keypoints
    for(int i = 0; i < featurePnts.size(); i++ ) {
       
        int diameter = int(featurePnts.at(i).size); // size of blob (= scale/diameter)
        cv::Point2f center = featurePnts.at(i).pt;

        int radius = diameter /2;
		int numBlocks = -1;
		int blocksPerSide = -1;
        int blockSide = -1;
        int blockSize = -1;

        // point too small
		if (diameter < mBLOCKSIZE_MIN) continue;

        // point exactly min or max block size
		if (diameter == mBLOCKSIZE_MIN || diameter == mBLOCKSIZE_MAX)
        {
            blockSide = diameter;
            blockSize = diameter;
            numBlocks = 1;
			blocksPerSide = 1;
        }
        else
        {
            // point between min and max block size
			if (diameter < mBLOCKSIZE_MAX)
			{
				blockSize = mBLOCKSIZE_MAX;
                blockSide = mBLOCKSIZE_MAX;
                numBlocks = 1;
				blocksPerSide = 1;

			}
            // point bigger than max block size
			else
			{
                blockSize = mBLOCKSIZE_MAX;
				blocksPerSide = ((diameter / mBLOCKSIZE_MAX) + 1);
				blockSide = blocksPerSide * blockSize;
                numBlocks = blocksPerSide * blocksPerSide;
			}

        }

        //TODO: remove debug
        #if defined DEBUG
            cv::Scalar color(rand() % 255,rand() % 255,rand() % 255);
            int x1 = center.x-blockSide/2;
            int x2 = center.y-blockSide/2;
            int debugsize = 20;
            int lineThickness = 2;
            if (blockSide > debugsize) cv::rectangle(blockImage, cv::Point2f(x1,x2), cv::Point2f(x1+blockSide,x2+blockSide), color, lineThickness);
            std::cout << "Keypoint(d = " << diameter << "): blockSize " << blockSize << ", #blocks: " << numBlocks << ", blockSide: " << blockSide  << std::endl;
        #endif


        // extract subpatch
        cv::Mat subPatch;
        cv::getRectSubPix(image_in, cv::Size(blockSide,blockSide), center, subPatch);


        cv::Mat comoDescriptor = cv::Mat::zeros(1, 144, CV_32F);

        // get blocksize x blocksize blocks from subpatch
        for (int i=0;i < blocksPerSide;i++)
        {
            for (int j = 0; j < blocksPerSide; j++)
            {


                cv::Mat block = subPatch(cv::Rect(i*blockSize,j*blockSize,blockSize,blockSize));
                cv::Mat grayBlock;
                convertToGrayscale(block, grayBlock);

                cv::Mat huMoments, grayscaleHist;

                calculateGrayscaleHistogram(grayBlock, grayscaleHist);
                float entropy = calculateEntropy(grayscaleHist, grayBlock.rows * grayBlock.cols);

                if (entropy >= 1)
                {
                    extractFromBlock(block, comoDescriptor);
                }

                // TODO: remove debug
                #if defined DEBUG
                    if (blockSide > debugsize)
                    {
                        cv::rectangle(blockImage, cv::Point2f(x1 + (i * blockSize), x2 + (j * blockSize)),
                                      cv::Point2f(x1 + (i * blockSize) + blockSize, x2 + (j * blockSize) + blockSize),
                                      color, lineThickness);

                    }
                #endif

            }
        }


        // create quantized descriptor
        cv::Mat comoDescriptorQuantized = cv::Mat::zeros(1, 144, CV_8UC1);
        double sum = cv::sum(comoDescriptor)[0];
        comoDescriptor /= sum;
        mQuantifier->quantify(comoDescriptor, comoDescriptorQuantized);

        // copy descriptor to result mat
        comoDescriptorQuantized.copyTo(descriptors);


    }

    //TODO: remove debug
    #if defined DEBUG
        //cv::imwrite("/Users/skletz/Dropbox/Programming/CUPCakes/opencv-como/debugging/test_block.jpg", debuggingImage);
        cv::imwrite("/media/bns/DATA/projects/opencv-como/debugging/al_block_test.jpg", blockImage);
    #endif

    status = true;
	return status;
}


bool como_engine::describe(cv::Mat& image, cv::Mat& descriptor)
{
	cv::Mat comoDescriptor = cv::Mat::zeros(1, 144, CV_32F);
	cv::Mat comoDescriptorQuantized = cv::Mat::zeros(1, 144, CV_8UC1);

	int width = image.cols;
	int height = image.rows;

	//1. Calculate grayscale histogram
	cv::Mat grayImage;
	convertToGrayscale(image, grayImage);

	int NumberOfBlocks = -1;
	if (std::min(width, height) >= 80)
		NumberOfBlocks = mBLOCKSIZE_MAX * mBLOCKSIZE_MAX;
	else if (std::min(width, height) >= 40)
		NumberOfBlocks = mBLOCKSIZE_MIN * mBLOCKSIZE_MIN;

	int Step_X = 2, Step_Y = 2, TemoMAX_X, TemoMAX_Y;
	if (NumberOfBlocks > 0) {
		double sqrtNumberOfBlocks = std::sqrt(NumberOfBlocks);
		Step_X = int(std::floor(width / sqrtNumberOfBlocks));
		Step_Y = int(std::floor(height / sqrtNumberOfBlocks));

		if ((Step_X % 2) != 0) {
			Step_X = Step_X - 1;
		}
		if ((Step_Y % 2) != 0) {
			Step_Y = Step_Y - 1;
		}

		TemoMAX_X = Step_X * int(sqrtNumberOfBlocks);
		TemoMAX_Y = Step_Y * int(sqrtNumberOfBlocks);
	}
	else {
		TemoMAX_X = Step_X * int(std::floor(width >> 1));
		TemoMAX_Y = Step_Y * int(std::floor(height >> 1));
	}

	// process image block by block
	for (int y = 0; y < TemoMAX_Y; y += Step_Y)
	{
		for (int x = 0; x < TemoMAX_X; x += Step_X)
		{
			cv::Mat imageBlock, grayBlock, huMoments, grayscaleHist;
			cv::Rect mask = cv::Rect(x, y, Step_X, Step_Y);
			grayImage(mask).copyTo(grayBlock);
			image(mask).copyTo(imageBlock);

			calculateGrayscaleHistogram(grayBlock, grayscaleHist);
			float entropy = calculateEntropy(grayscaleHist, grayBlock.rows * grayBlock.cols);

			if (entropy >= 1)
			{
				extractFromBlock(imageBlock, comoDescriptor);
			}
		}
	}

	double sum = cv::sum(comoDescriptor)[0];
	comoDescriptor /= sum;
	mQuantifier->quantify(comoDescriptor, comoDescriptorQuantized);
	comoDescriptorQuantized.copyTo(descriptor);
	return true;
}

bool como_engine::extractFromBlock(cv::Mat& imageBlock, cv::Mat& features)
{
	//int area = imageBlock.cols * imageBlock.rows;
	cv::Mat fuzzy10BinResult, fuzzy24BinResult;

	int huTableRows = sizeof(mTextureDefinitionTable) / sizeof(mTextureDefinitionTable[0]);
	int huTableCols = sizeof(mTextureDefinitionTable[0]) / sizeof(mTextureDefinitionTable[0][0]);
	cv::Mat huTable = cv::Mat(huTableRows, huTableCols, CV_64FC1, &mTextureDefinitionTable);

	std::vector<double> distances = std::vector<double>(huTableRows);
	std::vector<double> HSV = std::vector<double>(huTableRows);
	std::vector<double> edgeHist = std::vector<double>(huTableRows);

	std::vector<cv::Mat> channels;
	cv::Mat grayBlock, huMoments;
	convertToGrayscale(imageBlock, grayBlock);
	calculateHuMoments(grayBlock, huMoments);
	calculateDistances(huMoments, huTable, distances);

	//The type of texture the current block belongs to is determined.
	//A block can participate in more than one type of texture.
	int t = -1;
	for (int iDistance = 0; iDistance < distances.size(); iDistance++)
	{
		if (distances.at(iDistance) < mThresholdsTable[iDistance])
		{
			int pos = ++t;
			edgeHist[pos] = iDistance;
		}
	}

	cv::Scalar means = cv::mean(imageBlock);
	cv::Mat bgrmeans(1, 1, CV_8UC3, means);
	bgrmeans.convertTo(bgrmeans, CV_32F, 1. / 255);

	cv::Mat hsvmeans;
	cv::cvtColor(bgrmeans, hsvmeans, CV_BGR2HSV);

	cv::split(hsvmeans, channels);
	int h = channels[0].at<float>(0);
	int s = channels[1].at<float>(0) * 255;
	int v = channels[2].at<float>(0) * 255;

	mFuzzificator->calculateFuzzy10BinHisto(
		h, s, v, fuzzy10BinResult);
	mFuzzificator->calculateFuzzy24BinHisto(
		h, s, v, fuzzy10BinResult, fuzzy24BinResult);

	//t indicates, how many types of textures
	for (int i = 0; i <= t; i++)
	{
		for (int j = 0; j < 24; j++)
		{
			if (fuzzy24BinResult.at<double>(j) > 0)
			{
				//the index depends on the texture type, in which the fuzzy histogram will be stored.
				int index = 24 * edgeHist[i] + j;
				double fuzzy24Value = fuzzy24BinResult.at<double>(j);
				features.at<float>(index) += fuzzy24Value;
			}
		}
	}

	return true;
}

void como_engine::convertToGrayscale(cv::Mat& image, cv::Mat& gray)
{
	cv::cvtColor(image, gray, CV_BGR2GRAY);
}

void como_engine::calculateGrayscaleHistogram(cv::Mat& gray, cv::Mat& hist)
{
	float range[] = { 0, mGRAYHISTBINS - 1 };
	const float *ranges[] = { range };

	cv::Mat tmp;
    int tmpHISTBin = mGRAYHISTBINS;
	cv::calcHist(&gray, 1, 0, cv::Mat(), tmp, 1, &tmpHISTBin, ranges, true, false);

	//transpose histogram
	hist = tmp.col(0).t();
}

void como_engine::calculateHuMoments(cv::Mat& gray, cv::Mat& huMoments)
{
	double huMomArray[7]; //There are 7 HuMoments
	cv::HuMoments(cv::moments(gray), huMomArray);

	//transpose huMoments
	cv::Mat(1, 7, CV_64FC1, &huMomArray).copyTo(huMoments);
}

float como_engine::calculateEntropy(cv::Mat& grayscaleHist, int area)
{
	float entropy = 0.0;

	for (int iHist = 0; iHist < grayscaleHist.cols; iHist++)
	{
		float val = grayscaleHist.at<float>(iHist);
		if (val > 0)
		{
			val /= static_cast<float>(area);
			entropy -= val * (std::log(val) / std::log(2));
		}
	}

	return entropy;
}

void como_engine::calculateDistances(cv::Mat& huMoments, cv::Mat& huTable, std::vector<double>& distances)
{
	int huTableRows = huTable.rows;
	double maxDistance = (-1 * std::numeric_limits<double>::max());
	for (int iRow = 0; iRow < huTableRows; iRow++)
	{
		cv::Mat row = huTable.row(iRow);
		double distance = cv::norm(huMoments, row);
		distances.at(iRow) = distance;
		if (distance > maxDistance)
		{
			maxDistance = distances.at(iRow);
		}
	}

	for (int i = 0; i < distances.size(); i++)
	{
		distances.at(i) /= maxDistance;
	}
}

void como_engine::showHistogram(std::string name, cv::MatND &hist, int height, int width)
{
	//get the maximal height of the histogram
	double maxVal = 0;
	minMaxLoc(hist, 0, &maxVal, 0, 0);

	int scale = 10;
	cv::Mat histImg = cv::Mat::zeros(256, maxVal * scale, CV_8UC1);

	for (int iEntry = 0; iEntry < mGRAYHISTBINS; iEntry++){
		float binVal = hist.at<float>(iEntry,0);
		int intensity = cvRound(binVal * (mGRAYHISTBINS - 1) / maxVal);
		rectangle(histImg, cv::Point(iEntry*scale, histImg.rows), cv::Point((iEntry + 1)*scale - 1, histImg.rows - intensity),  cv::Scalar::all(255), CV_FILLED);
	}

	cv::namedWindow(name);
	cv::imshow(name, histImg);
}

void como_engine::showImage(std::string name, cv::MatND &image)
{
	cv::namedWindow(name);
	cv::imshow(name, image);
}

//deletable, used for debugging only
bool como_engine::test(cv::Mat& image, cv::Mat& descriptor)
{
	int width = image.cols;
	int height = image.rows;

	//1. Calculate grayscale histogram
	cv::Mat grayImage;
	convertToGrayscale(image, grayImage);
	//calculateGrayscaleHistogram(grayImage, grayscaleHist);

	int NumberOfBlocks = -1;
	if (std::min(width, height) >= 80)
		NumberOfBlocks = 1600;
	else if (std::min(width, height) >= 40)
		NumberOfBlocks = 400;

	int Step_X = 2, Step_Y = 2, TemoMAX_X, TemoMAX_Y;
	if (NumberOfBlocks > 0) {
		double sqrtNumberOfBlocks = std::sqrt(NumberOfBlocks);
		Step_X = int(std::floor(width / sqrtNumberOfBlocks));
		Step_Y = int(std::floor(height / sqrtNumberOfBlocks));

		if ((Step_X % 2) != 0) {
			Step_X = Step_X - 1;
		}
		if ((Step_Y % 2) != 0) {
			Step_Y = Step_Y - 1;
		}

		TemoMAX_X = Step_X * int(sqrtNumberOfBlocks);
		TemoMAX_Y = Step_Y * int(sqrtNumberOfBlocks);
	}
	else {
		TemoMAX_X = Step_X * int(std::floor(width >> 1));
		TemoMAX_Y = Step_Y * int(std::floor(height >> 1));
	}

	//std::vector<double> comoDescriptor = std::vector<double>(144);
	cv::Mat comoDescriptor = cv::Mat::zeros(1, 144, CV_32F);
	cv::Mat comoDescriptorQuantized = cv::Mat::zeros(1, 144, CV_8UC1);
	//int T,
	int area = Step_Y * Step_X;

	int huTableRows = sizeof(mTextureDefinitionTable) / sizeof(mTextureDefinitionTable[0]);
	int huTableCols = sizeof(mTextureDefinitionTable[0]) / sizeof(mTextureDefinitionTable[0][0]);
	cv::Mat huTable = cv::Mat(huTableRows, huTableCols, CV_64FC1, &mTextureDefinitionTable);

	std::vector<double> distances = std::vector<double>(huTableRows);
	std::vector<double> HSV = std::vector<double>(huTableRows);
	std::vector<double> edgeHist = std::vector<double>(huTableRows);

	cv::Mat fuzzy10BinResult, fuzzy24BinResult;

	std::vector<cv::Mat> channels;
	for (int y = 0; y < TemoMAX_Y; y += Step_Y)
	{
		for (int x = 0; x < TemoMAX_X; x += Step_X)
		{
			cv::Mat imageBlock, grayBlock, huMoments, grayscaleHist;
			cv::Rect mask = cv::Rect(x, y, Step_X, Step_Y);
			grayImage(mask).copyTo(grayBlock);
			image(mask).copyTo(imageBlock);

			calculateGrayscaleHistogram(grayBlock, grayscaleHist);
			float entropy = calculateEntropy(grayscaleHist, area);

			if (entropy >= 1)
			{
				calculateHuMoments(grayBlock, huMoments);
				calculateDistances(huMoments, huTable, distances);

				//The type of texture the current block belongs to is determined.
				//A block can participate in more than one type of texture.
				int t = -1;
				for (int iDistance = 0; iDistance < distances.size(); iDistance++)
				{
					if (distances.at(iDistance) < mThresholdsTable[iDistance])
					{
						int pos = ++t;
						edgeHist[pos] = iDistance;
					}
				}

				cv::Scalar means = cv::mean(imageBlock);
				cv::Mat bgrmeans(1, 1, CV_8UC3, means);
				bgrmeans.convertTo(bgrmeans, CV_32F, 1. / 255);

				cv::Mat hsvmeans;
				cv::cvtColor(bgrmeans, hsvmeans, CV_BGR2HSV);

				cv::split(hsvmeans, channels);
				int h = channels[0].at<float>(0);
				int s = channels[1].at<float>(0) * 255;
				int v = channels[2].at<float>(0) * 255;

				mFuzzificator->calculateFuzzy10BinHisto(
					h, s, v, fuzzy10BinResult);
				mFuzzificator->calculateFuzzy24BinHisto(
					h, s, v, fuzzy10BinResult, fuzzy24BinResult);

				//t indicates, how many types of textures
				for (int i = 0; i <= t; i++)
				{
					for (int j = 0; j < 24; j++)
					{
						if (fuzzy24BinResult.at<double>(j) > 0)
						{
							//the index depends on the texture type, in which the fuzzy histogram will be stored.
							int index = 24 * edgeHist[i] + j;
							double fuzzy24Value = fuzzy24BinResult.at<double>(j);
							comoDescriptor.at<float>(index) += fuzzy24Value;
						}
					}
				}
			}
		}
	}

	double sum = 0.0;
	for (int i = 0; i < 144; i++)
	{
		sum += comoDescriptor.at<float>(i);
	}

	for (int i = 0; i < 144; i++)
	{
		comoDescriptor.at<float>(i) /= sum;
	}

	mQuantifier->quantify(comoDescriptor, comoDescriptorQuantized);
	return true;
}
