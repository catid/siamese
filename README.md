# Siamese
## Fast and Portable Real-Time Streaming Erasure Correction Codes in C

Siamese is a fast and portable library for Erasure Correction.
From a real-time stream of input data it generates redundant data that can be used to
recover the lost originals without using acknowledgements.

Siamese is a streaming erasure code designed for low to medium rate streams
under 2000 packets per RTT (~3MB/s on the Internet), and modest loss rates
like 20% or less.  It works on mobile phones and mac/linux/windows computers.


##### What's unique about this approach?

This library implements a new type of convolutional code for erasure correction.
Since it is not a block code, as soon as recovery data arrives it can be used to recover original data.
The advantages this provides are explained thoroughly in this article:

[Block or Convolutional AL-FEC Codes? A Performance
Comparison for Robust Low-Latency Communications](https://hal.inria.fr/hal-01395937v2/document) [1].

~~~
[1] Vincent Roca, Belkacem Teibi, Christophe Burdinat, Tuan Tran-Thai, C´edric Thienot. Block
or Convolutional AL-FEC Codes? A Performance Comparison for Robust Low-Latency Communications.
2017. <hal-01395937v2>
~~~

For each recovery symbol that it produces, it stores and reuses some of the intermediate work, so that producing the next symbol takes much less time than usual.  As a side-effect, this approach only works for full (not partial) reliable data delivery.

There is also a block code (rather than streaming) implementation here: [fecal](https://github.com/catid/fecal).  It's easier to understand the new convolutional code math by reading through the encoder of that software and its readme.


##### Target application: Writing a rUDP Protocol

* If you're writing your own reliable UDP protocol, this can save you a bunch
of time for the trickier parts of the code to write.

It can also generate selective acknowledgements and retransmitted data to be
useful as the core engine of a Hybrid ARQ transport protocol, and it exposes
its custom memory allocator to help implement outgoing data queues.


##### Example Usage:

Example codec usage with error checking omitted:

~~~
SiameseEncoder encoder = siamese_encoder_create();
SiameseDecoder decoder = siamese_decoder_create();

// For each original datagram:

SiameseOriginalPacket original;
original.Data = buffer;
original.DataBytes = bytes;

siamese_encoder_add(encoder, &original);
siamese_decoder_add_original(decoder, &original);

// For each recovery datagram:

SiameseRecoveryPacket recovery;
siamese_encode(encoder, &recovery);

siamese_decoder_add_recovery(decoder, &recovery);

if (0 == siamese_decoder_is_ready(decoder))
{
    SiameseOriginalPacket* recovered = nullptr;
    unsigned recoveredCount = 0;
    siamese_decode(decoder, &recovered, &recoveredCount);

    // Process recovered data here.
}
~~~
        
There are more detailed examples in [unit_test.cpp](https://github.com/catid/siamese/blob/master/tests/unit_test.cpp).


#### Comparisons

Siamese is fairly different from the other erasure coding software I've released.

All the other ones are block codes, meaning they take a block of input at a time.  Summary:

[cm256](https://github.com/catid/cm256) : GF(256) Cauchy Reed-Solomon block code.  Limited to 255 inputs or outputs.  Input data cannot change between outputs.  Recovery never fails.

[longhair](https://github.com/catid/longhair) : Binary(XOR-only) Cauchy Reed-Solomon block code.  Limited to 255 inputs or outputs.  Inputs must be a multiple of 8 bytes.  Input data cannot change between outputs.  Recovery never fails.

[wirehair](https://github.com/catid/wirehair) : Complex LDPC+HDPC block code.  Up to 64,000 inputs in a block.  Unlimited outputs.  Decoder takes about the same time regardless of number of losses, implying that one lost packet takes a long time to recover.  Input data cannot change between outputs.  Recovery can fail about 1% of the time.

Siamese : Artifically limited to 16,000 inputs.  Artificially limited to 256 outputs.  Inputs *can* change between outputs.  Decoder takes time proportional to the number of losses as O(N^2).

For small loss count it's faster than Wirehair, and past a certain point (~10% loss) encode+decode time is longer than Wirehair.

At lower data rates, Siamese uses a Cauchy Reed-Solomon code: Recovery never fails.
At higher data rates, Siamese switches to a new structured linear convolutional code: It fails to recover about 1% of the time.

Many of the parameters of the code are tunable to trade between performance and recovery rate.


#### How Siamese works

The library uses Siamese Codes for a structured convolutional matrix. This matrix has a fast matrix-vector product involving mostly XOR operations. This allows Siamese Codes to encode and decode much faster than other convolutional codes built on Cauchy or Vandermonde matrices. Let's call this the Siamese Matrix Structure or something similar.

To produce an output packet, some preprocessing is performed.

The input data is first split into 8 "lanes" where every 8th symbol {e.g. 0, 8, 16, 24, ...} is summed together. The second "lane" starts from input symbol 1 and contains every 8th symbol after that {e.g. 1, 9, 17, 25, ...}.

For each "lane" there are three running "sums":

Sum 0: Simple XOR between all inputs in that lane.
Sum 1: Each input is multiplied by a coefficient provided by GetColumnValue, and then XORed into the sum.
Sum 2: Each input is multiplied by the same coefficient squared, and then XORed into the sum.
This means there are 24 running sums, each with symbol_bytes bytes of data.

When an output is being produced (encoded), two running sums are formed temporarily. Both are generated through the same process, and the result of one sum is multiplied by a row coefficient produced by the GetRowValue function and added to the other sum to produce the output.

To produce each of the two sums, a formula is followed. For each lane, the GetRowOpcode function returns which sums should be used. Sums 0, 1, and 2 are incorporated in based on the function output. And then 1/16 of the input data are selected at random and XORed into each sum.

The Siamese codec and the Fecal decoder both will compute lane sums only when they are needed. Since some of the 24 sums (about 50%) are unneeded, the number of operations will vary for each row.

The final random XOR is similar to an LDPC code and allows the recovery properties of the code to perform well on a larger scale above about 32 input symbols. The GF(2^^8) multiplies dominate the recovery properties for smaller losses and input symbols. The specific code used was selected by experimenting with different parameters until a desired failure rate was achieved with good performance characteristics.

As a result the Siamese Codes mainly use XORs. So it can run a lot faster than straight GF(2^^8) multiply-add operations. Since they are still Convolutional Codes, the Siamese Codes also lend themselves to streaming use case.

When AVX2 and SSSE3 are unavailable, Siamese takes 4x longer to decode and 2.6x longer to encode. Encoding requires a lot more simple XOR ops so it is still pretty fast. Decoding is usually really quick because average loss rates are low, but when needed it requires a lot more GF multiplies requiring table lookups which is slower.


#### Credits

Software by Christopher A. Taylor mrcatid@gmail.com

Please reach out if you need support or would like to collaborate on a project.
