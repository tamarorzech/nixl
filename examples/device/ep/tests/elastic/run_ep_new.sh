#!/bin/sh

NUM_TOPK="${2:-8}"
NUM_TOKENS="${3:-128}"
NUM_PROCESSES="${4:-8}"
NUM_EXPERT_PER_RANK="${5:-36}"
RUN_NUMBER="${6:-1}"
TITLE="${7:-}"

if [ "$1" == "baseline" ]; then
    mkdir -p /workspace/external/sw_contentions_results/"$TITLE"/random/ep_8_tokens_"$NUM_TOKENS"_topk_"$NUM_TOPK"/run_"$RUN_NUMBER"
    mkdir -p /workspace/external/sw_contentions_results/"$TITLE"/fixed_allocation/ep_8_tokens_"$NUM_TOKENS"_topk_"$NUM_TOPK"/run_"$RUN_NUMBER"
    rm -rf /workspace/external/sw_contentions_results/"$TITLE"/random/ep_8_tokens_"$NUM_TOKENS"_topk_"$NUM_TOPK"/run_"$RUN_NUMBER"/*
    rm -rf /workspace/external/sw_contentions_results/"$TITLE"/fixed_allocation/ep_8_tokens_"$NUM_TOKENS"_topk_"$NUM_TOPK"/run_"$RUN_NUMBER"/*

    # iterate over NUM_CHANNELS [4, 8, 16, 32]
    for NUM_CHANNELS in 4 8 16 32; do
        echo "NIXL_EP_NUM_CHANNELS=$NUM_CHANNELS PYTHONPATH=/workspace/external/nixl/build/examples/device/ep:$(pwd):/workspace/external/nixl/examples/device/ep/tests LD_LIBRARY_PATH=/workspace/external/ucx/lib:\$LD_LIBRARY_PATH python3 ./elastic.py --num-tokens \"$NUM_TOKENS\" --hidden-dim 7168 --num-topk \"$NUM_TOPK\" --num-processes \"$NUM_PROCESSES\" --num-experts-per-rank \"$NUM_EXPERT_PER_RANK\" --plan 8_no_expansion.json --disable-ll-nvlink --kineto | tee /workspace/external/sw_contentions_results/\"$TITLE\"/random/ep_8_tokens_\"$NUM_TOKENS\"_topk_\"$NUM_TOPK\"/run_\"$RUN_NUMBER\"/elastic_output_num_channels_\"$NUM_CHANNELS\".txt"
        NIXL_EP_NUM_CHANNELS=$NUM_CHANNELS \
        PYTHONPATH=/workspace/external/nixl/build/examples/device/ep:$(pwd):/workspace/external/nixl/examples/device/ep/tests \
        LD_LIBRARY_PATH=/workspace/external/ucx/lib:$LD_LIBRARY_PATH \
        python3 ./elastic.py --num-tokens "$NUM_TOKENS" --hidden-dim 7168 \
        --num-topk "$NUM_TOPK"  --num-processes "$NUM_PROCESSES" --num-experts-per-rank "$NUM_EXPERT_PER_RANK" \
        --plan 8_no_expansion.json --disable-ll-nvlink  --kineto | tee /workspace/external/sw_contentions_results/"$TITLE"/random/ep_8_tokens_"$NUM_TOKENS"_topk_"$NUM_TOPK"/run_"$RUN_NUMBER"/elastic_output_num_channels_"$NUM_CHANNELS".txt

        echo "NIXL_EP_NUM_CHANNELS=$NUM_CHANNELS PYTHONPATH=/workspace/external/nixl/build/examples/device/ep:$(pwd):/workspace/external/nixl/examples/device/ep/tests LD_LIBRARY_PATH=/workspace/external/ucx/lib:\$LD_LIBRARY_PATH python3 ./elastic_constant_routing.py --num-tokens \"$NUM_TOKENS\" --hidden-dim 7168 --num-topk \"$NUM_TOPK\" --num-processes \"$NUM_PROCESSES\" --num-experts-per-rank \"$NUM_EXPERT_PER_RANK\" --plan 8_no_expansion.json --disable-ll-nvlink --kineto | tee /workspace/external/sw_contentions_results/\"$TITLE\"/fixed_allocation/ep_8_tokens_\"$NUM_TOKENS\"_topk_\"$NUM_TOPK\"/run_\"$RUN_NUMBER\"/elastic_constant_routing_output_num_channels_\"$NUM_CHANNELS\".txt"
        NIXL_EP_NUM_CHANNELS=$NUM_CHANNELS \
        PYTHONPATH=/workspace/external/nixl/build/examples/device/ep:$(pwd):/workspace/external/nixl/examples/device/ep/tests \
        LD_LIBRARY_PATH=/workspace/external/ucx/lib:$LD_LIBRARY_PATH \
        python3 ./elastic_constant_routing.py --num-tokens "$NUM_TOKENS" --hidden-dim 7168 \
        --num-topk "$NUM_TOPK"  --num-processes "$NUM_PROCESSES" --num-experts-per-rank "$NUM_EXPERT_PER_RANK" \
        --plan 8_no_expansion.json --disable-ll-nvlink  --kineto | tee /workspace/external/sw_contentions_results/"$TITLE"/fixed_allocation/ep_8_tokens_"$NUM_TOKENS"_topk_"$NUM_TOPK"/run_"$RUN_NUMBER"/elastic_constant_routing_output_num_channels_"$NUM_CHANNELS".txt
    done

fi

if [ "$1" == "fixed_alloc" ]; then
    mkdir -p /workspace/external/sw_contentions_results/"$TITLE"/fixed_allocation/ep_8_tokens_"$NUM_TOKENS"_topk_"$NUM_TOPK"/run_"$RUN_NUMBER"
    rm -rf /workspace/external/sw_contentions_results/"$TITLE"/fixed_allocation/ep_8_tokens_"$NUM_TOKENS"_topk_"$NUM_TOPK"/run_"$RUN_NUMBER"/*

    # iterate over NUM_CHANNELS [4, 8, 16, 32]
    for NUM_CHANNELS in 32; do
        echo "NIXL_EP_NUM_CHANNELS=$NUM_CHANNELS PYTHONPATH=/workspace/external/nixl/build/examples/device/ep:$(pwd):/workspace/external/nixl/examples/device/ep/tests LD_LIBRARY_PATH=/workspace/external/ucx/lib:\$LD_LIBRARY_PATH python3 ./elastic_constant_routing.py --num-tokens \"$NUM_TOKENS\" --hidden-dim 7168 --num-topk \"$NUM_TOPK\" --num-processes \"$NUM_PROCESSES\" --num-experts-per-rank \"$NUM_EXPERT_PER_RANK\" --plan 8_no_expansion.json --disable-ll-nvlink --kineto | tee /workspace/external/sw_contentions_results/\"$TITLE\"/fixed_allocation/ep_8_tokens_\"$NUM_TOKENS\"_topk_\"$NUM_TOPK\"/run_\"$RUN_NUMBER\"/elastic_constant_routing_output_num_channels_\"$NUM_CHANNELS\".txt"
        NIXL_EP_NUM_CHANNELS=$NUM_CHANNELS \
        PYTHONPATH=/workspace/external/nixl/build/examples/device/ep:$(pwd):/workspace/external/nixl/examples/device/ep/tests \
        LD_LIBRARY_PATH=/workspace/external/ucx/lib:$LD_LIBRARY_PATH \
        python3 ./elastic_constant_routing.py --num-tokens "$NUM_TOKENS" --hidden-dim 7168 \
        --num-topk "$NUM_TOPK"  --num-processes "$NUM_PROCESSES" --num-experts-per-rank "$NUM_EXPERT_PER_RANK" \
        --plan 8_no_expansion.json --disable-ll-nvlink  --kineto | tee /workspace/external/sw_contentions_results/"$TITLE"/fixed_allocation/ep_8_tokens_"$NUM_TOKENS"_topk_"$NUM_TOPK"/run_"$RUN_NUMBER"/elastic_constant_routing_output_num_channels_"$NUM_CHANNELS".txt
    done

fi

if [ "$1" == "32SMs_w_masking_org_syncs" ]; then
    # Run each configuration 6 times
    for RUN in $(seq 1 6); do
        echo "========================================="
        echo "Starting Run #$RUN of 6"
        echo "========================================="
        
        ./run_ep_new.sh baseline 8 128 8 32 "$RUN" "$1"

        ./run_ep_new.sh baseline 8 256 8 32 "$RUN" "$1"

        ./run_ep_new.sh baseline 8 512 8 32 "$RUN" "$1"

        ./run_ep_new.sh baseline 8 1024 8 32 "$RUN" "$1"
        
        echo "Completed Run #$RUN of 6"
        echo ""
    done
    
    echo "========================================="
    echo "All 6 runs completed successfully!"
    echo "========================================="
fi

if [ "$1" == "32SMs_w_masking_reduced_syncs" ]; then
    # Run each configuration 6 times
    for RUN in $(seq 1 6); do
        echo "========================================="
        echo "Starting Run #$RUN of 6"
        echo "========================================="
        
        ./run_ep_new.sh fixed_alloc 8 128 8 32 "$RUN" "$1"

        ./run_ep_new.sh fixed_alloc 8 256 8 32 "$RUN" "$1"

        ./run_ep_new.sh fixed_alloc 8 512 8 32 "$RUN" "$1"

        ./run_ep_new.sh fixed_alloc 8 1024 8 32 "$RUN" "$1"
        
        echo "Completed Run #$RUN of 6"
        echo ""
    done
    
    echo "========================================="
    echo "All 6 runs completed successfully!"
    echo "========================================="
fi