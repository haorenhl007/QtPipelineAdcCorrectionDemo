#include <QCoreApplication>
#include <QDebug>

#include <iostream>
#include <stdexcept>
#include <Qstring>

using namespace std;


#define PIPELINED_ADC_NUMBER_ADC_STAGES 10
#define PIPELINED_ADC_NUMBER_OF_CODES 3

class PipelinedAdc
{

public:
    const int STAGE_BITS_ONE = 1;
    const int STAGE_BITS_ONE_P_FIVE = 2;

    int VerboseDebugEnabled = 0;

    double StageResidueGainErrorInject[PIPELINED_ADC_NUMBER_ADC_STAGES];

    double StageOffsetError[PIPELINED_ADC_NUMBER_ADC_STAGES];

    double StageAnalogInput[PIPELINED_ADC_NUMBER_ADC_STAGES];
    int StageDigitalCodesToOutput[PIPELINED_ADC_NUMBER_OF_CODES] = { -1, 0, 1 };
    double StageFlashThresholdVoltages[PIPELINED_ADC_NUMBER_OF_CODES] = { -0.25, 0.25, 999 };

    int StageActualCodeThisConversion[PIPELINED_ADC_NUMBER_ADC_STAGES];
    int StageActualDigitalIndexThisConversion[PIPELINED_ADC_NUMBER_ADC_STAGES];


    int PerformConversion(double valueToConvert)
    {

        double previousStageOutput;
        previousStageOutput = valueToConvert;

        if (VerboseDebugEnabled)
            qDebug() << "\n\nConversion of number: " << valueToConvert << "\n";

        if (VerboseDebugEnabled)
            for (int codeNumber = 0; codeNumber < PIPELINED_ADC_NUMBER_OF_CODES; codeNumber++)
                qDebug() << "   Threshold for code " << StageDigitalCodesToOutput[codeNumber] << " is " << StageFlashThresholdVoltages[codeNumber] << "\n";


        for (int stageNumber = 0; stageNumber < PIPELINED_ADC_NUMBER_ADC_STAGES; stageNumber++)
        {
            // Get input to stage
            StageAnalogInput[stageNumber] = previousStageOutput;
            double currentStageInput = previousStageOutput;

            // Find which code to output
            for (int codeNumber = 0; codeNumber < PIPELINED_ADC_NUMBER_OF_CODES; codeNumber++)
            {
                // Below ensures if loop falls through then we output highest code number

                double t = StageFlashThresholdVoltages[codeNumber];

                // For example, if t=0.5 for this code, that means the code covers any case where value is less than
                // 0.5.  So if value is less than threshold, immediately call it.
                if ((currentStageInput + StageOffsetError[stageNumber]) < t)
                {
                    StageActualCodeThisConversion[stageNumber] = StageDigitalCodesToOutput[codeNumber];
                    StageActualDigitalIndexThisConversion[stageNumber] = codeNumber;
                    break;
                }

            }

            int currentCode = StageActualCodeThisConversion[stageNumber];

            double residue = 0;

            if (currentCode == -1)
                residue = 2 * currentStageInput + 1;
            else if (currentCode == 0)
                residue = 2 * currentStageInput;
            else if (currentCode == 1)
                residue = 2 * currentStageInput - 1;
            else throw runtime_error("Invalid stage code found");

            previousStageOutput = residue * StageResidueGainErrorInject[stageNumber];

            if (VerboseDebugEnabled)
                qDebug() << "Stage: " << stageNumber << "Input V=" << currentStageInput << "Code is " << currentCode << "ResidueOut is " << previousStageOutput << "\n";
        }

        if (VerboseDebugEnabled)
            qDebug() << "ReturnValue " << GetTotalCode() << "\n";

        return GetTotalCode();
    };

    QString GetCodesAsString()
    {
        QString rtn = "";
        for (int stageNumber = 0; stageNumber < PIPELINED_ADC_NUMBER_ADC_STAGES; stageNumber++)
            rtn +=  QString::number(StageActualDigitalIndexThisConversion[stageNumber]);
        return rtn;			// Return as value reference as rtn goes out of scope
    }

private:

    int GetTotalCode()
    {
        int returnValue = 0;
        int currentBitMask = (1 << (PIPELINED_ADC_NUMBER_ADC_STAGES - 1));
        int offsetAdjust = (1 << PIPELINED_ADC_NUMBER_ADC_STAGES) - 1;

        for (int stageNumber = 0; stageNumber < PIPELINED_ADC_NUMBER_ADC_STAGES; stageNumber++)
        {
            returnValue += currentBitMask * StageActualCodeThisConversion[stageNumber];
            currentBitMask = currentBitMask >> 1;
        }
        return (returnValue + offsetAdjust) >> 1;
    }

};




int main(int argc, char *argv[])
{
    QCoreApplication a(argc, argv);

    PipelinedAdc adc;
    adc.VerboseDebugEnabled = 0;

    // ------------------------------------------------------------------------------------------
    // -             Setup ideal ADC
    // -------------------------------------------------------------------------------------------
    for (auto i = 0; i < PIPELINED_ADC_NUMBER_ADC_STAGES; i++)
        adc.StageResidueGainErrorInject[i] = 1.00;

    for (auto i = 0; i < PIPELINED_ADC_NUMBER_ADC_STAGES; i++)
        adc.StageOffsetError[i] = 0.00;


    QString listPipelineStageValuesWithNoErrors[1025];

    qDebug() << "Demo for Correction of Comparator Threshold Errors in 10-Stage 1.5 bit/stage Pipleined ADC \n\n";

    // ------------------------------------------------------------------------------------------
    // -             Some easily tracked numbers - used for debugging
    // -------------------------------------------------------------------------------------------
    /*
    adc.PerformConversion(0.6);

    adc.PerformConversion(-1);
    adc.PerformConversion(1);
    adc.PerformConversion(0);
    adc.PerformConversion(0.01);
    adc.PerformConversion(-0.01);
    */

    // ------------------------------------------------------------------------------------------
    // -             Setup some general values we will use for calculating INL, DNL
    // -------------------------------------------------------------------------------------------

    double oneLsbVolt = (2.00 / 1024);
    double lowestVoltageToCheck = -1.00;
    double highestVoltageToCheck = 1.00;

    // Calculate lowest and highest code for INL performance
    int lowestCode = adc.PerformConversion(lowestVoltageToCheck);
    int highestCode = adc.PerformConversion(highestVoltageToCheck);
    double numberCodesPerUnitVoltDouble = (double)(highestCode - lowestCode + 1) / (highestVoltageToCheck - lowestVoltageToCheck);
    double voltsPerCodeDouble = 1.00 / numberCodesPerUnitVoltDouble;

    // Calculate lowest and highest code for INL performance
    qDebug() << "Lowest Code " << lowestCode << " and highestCode " << highestCode << "\n";

    // ------------------------------------------------------------------------------------------
    // -             Create a reference set of internal code stage values so we can compare these + highlight correction process
    // -------------------------------------------------------------------------------------------
    {
        int sampleCount = 0;

        for (double inputVoltage = lowestVoltageToCheck + voltsPerCodeDouble; inputVoltage <= highestVoltageToCheck; inputVoltage += voltsPerCodeDouble)
        {
            adc.PerformConversion(inputVoltage);
            listPipelineStageValuesWithNoErrors[sampleCount++] = adc.GetCodesAsString();
        }
    }

    // ------------------------------------------------------------------------------------------
    // -                                    INTRODUCE ERRORS INTO ADC PIPELINE
    // -------------------------------------------------------------------------------------------
    // adc.StageResidueGainErrorInject[2] = 1.1;
    adc.StageOffsetError[1] = 0.2;
\
    // ------------------------------------------------------------------------------------------
    // -                            Perform INL and DNL experiment
    // -------------------------------------------------------------------------------------------

    // For simplicity, just take 2^N sample points spaced evenly over ADC range.  Better Approach =
    // to check additional voltages within range to establish transition points better

    int actualAdcCode, previousOutputCode = -1;
    double INL = 0, DNL = 0;

    double worstDNL = 0, worstINL = 0;
    int sampleCount = 0;

    for (double inputVoltage = lowestVoltageToCheck + voltsPerCodeDouble; inputVoltage <= highestVoltageToCheck; inputVoltage += voltsPerCodeDouble)
    {
        // Remap inputCount to -1 to +1
        actualAdcCode = adc.PerformConversion(inputVoltage);

        // Do straight line fit from inputVoltage range (-1...1) to code range (lowestCode + highestCode)
        double voltageDeltaFromLowest = (inputVoltage - (-1.00));

        double idealAdcCode = (double)lowestCode + (voltageDeltaFromLowest / voltsPerCodeDouble) - 1;

        int expectedCodeForDNL = previousOutputCode + 1;

        DNL = (actualAdcCode - (previousOutputCode + 1));
        INL = actualAdcCode - idealAdcCode;

        QString codes = adc.GetCodesAsString();

        QString valueIdealAdc = listPipelineStageValuesWithNoErrors[sampleCount];

        if (codes.compare(valueIdealAdc) != 0)
            codes += "  w/out ERRORS = " + valueIdealAdc;


        qDebug() << "Input=" << inputVoltage << "IdealCode=" << idealAdcCode << "ActualCode=" <<actualAdcCode << "DNL=" << DNL << "INL=" << INL << "Codes = " << codes;

        previousOutputCode = actualAdcCode;
        if (DNL > worstDNL)
            worstDNL = DNL;
        if (INL > worstINL)
            worstINL = INL;

        sampleCount++;
    }

    // ------------------------------------------------------------------------------------------
    // -                            Report final values
    // -------------------------------------------------------------------------------------------

    qDebug() << "\n\n\nPERFORMANCE METRICS: \nWorstcase DNL=" << worstDNL << ", worstcase INL= " << worstINL << " \n";


    return a.exec();
}
